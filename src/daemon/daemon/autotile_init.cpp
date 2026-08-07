// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Daemon::initializeAutotile() — the autotile engine's signal wiring and its
// eight shortcut handlers — plus Daemon::handleTilingModeToggle(), the
// three-mode cycle the toggle shortcut drives. Split out of daemon/signals.cpp
// to keep that file under the size ceiling; the rest of the daemon's signal
// wiring stays there.

#include "daemon/daemon.h"
#include "helpers.h"
#include "config/settings.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include "core/resolve/screenmoderouter.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/overlayservice.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorContext/ContextResolver.h>
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorZones/LayoutRegistry.h>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QVector>

#include <algorithm>
#include <utility>

using PlacementEngineBase = PhosphorEngine::PlacementEngineBase;

namespace PlasmaZones {

void Daemon::initializeAutotile()
{
    // The shortcut handlers below hang off m_shortcutManager, which is
    // ctor-owned and never reset, so a stop() -> init() -> start() cycle would
    // stack a second copy of each and fire every autotile shortcut twice. Drop
    // exactly the handles we installed last time, for the reason
    // connectLayoutSignals() does: a (sender, signal, receiver) disconnect
    // would also delete other call sites' handlers, and Qt::UniqueConnection
    // does not apply to lambda/functor connections. The engine handlers need
    // no such treatment — stop() destroys m_autotileEngine along with its
    // connections, so that block only runs against a freshly built engine.
    for (const QMetaObject::Connection& c : std::as_const(m_autotileShortcutConnections)) {
        disconnect(c);
    }
    m_autotileShortcutConnections.clear();

    // Connect autotile engine signals
    if (m_autotileEngine) {
        // Autotile engine signals → OSD (use display name, not algorithm ID)
        // Show OSD when algorithm changes (not on every retile — tilingChanged
        // fires for float, swap, window open/close, etc. which is too noisy)
        connect(m_autotileEngine.get(), &PlacementEngineBase::algorithmChanged, this,
                [this](const QString& algorithmId) {
                    // Suppress during startup: loadState() emits algorithmChanged from
                    // finalizeStartup(), and finalizeStartup() is the authoritative
                    // startup-OSD path (gated on showOsdOnDesktopSwitch via
                    // showOsdForAllScreens). Letting this handler also fire would both
                    // double-queue an OSD on the focused screen AND leak past
                    // showOsdOnDesktopSwitch=false, since this branch gates only on
                    // showOsdOnLayoutSwitch.
                    //
                    // Also gate on isAnyScreenAutotile() — loadState() may emit even
                    // when no screen is in autotile mode, and a runtime algorithm
                    // change is irrelevant in that case.
                    if (m_running && isAnyScreenAutotile() && m_settings && m_settings->showOsdOnLayoutSwitch()
                        && m_overlayService) {
                        auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(algorithmId) : nullptr;
                        QString displayName = algo ? algo->name() : algorithmId;
                        QString screenId;
                        if (m_autotileEngine) {
                            screenId = m_autotileEngine->activeScreen();
                        }
                        if (screenId.isEmpty() && m_unifiedLayoutController) {
                            screenId = m_unifiedLayoutController->currentScreenName();
                        }
                        if (screenId.isEmpty() && m_windowTrackingAdaptor) {
                            screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
                        }
                        showAlgorithmOsdDeferred(algorithmId, displayName, screenId);
                    }
                });

        // Sync autotile float state and show OSD when a window is floated/unfloated
        connect(m_autotileEngine.get(), &PhosphorEngine::PlacementEngineBase::windowFloatingChanged, this,
                &Daemon::syncAutotileFloatState);

        // Passive float state sync for engine-internal state divergence (e.g. a
        // newly-inserted window carrying stale snap-mode float state). Routes to
        // a handler that updates WTS bookkeeping without calling
        // applyGeometryForFloat — the window already has a valid position and
        // must not be teleported to a stored pre-tile rect.
        connect(m_autotileEngine.get(), &PhosphorEngine::PlacementEngineBase::windowFloatingStateSynced, this,
                &Daemon::syncAutotileFloatStatePassive);

        // Batch overflow float handler: overflow windows are included in the
        // windowsTileRequested D-Bus signal with "floating" flag, so the effect
        // handles geometry restore directly. Here we only update daemon-side
        // WTS state without emitting per-window D-Bus signals.
        connect(m_autotileEngine.get(), &PhosphorEngine::PlacementEngineBase::windowsBatchFloated, this,
                &Daemon::syncAutotileBatchFloatState);

        // Per-mode float state: when autotile releases windows back to snap mode:
        // 1. Clear autotile-originated floats (overflow + user-float-in-autotile) —
        //    autotile floats don't persist into snap mode. The autotile slot in each
        //    window's placement record was snapshotted at release (setAutotileScreens)
        //    so re-entry restores from it.
        // 2. Restore snap-mode floats from each window's placement record (snap slot),
        //    captured when the screen last left snapping — the single source of truth.
        // Must check isModeSpecificFloated BEFORE clearing the marker.
        connect(m_autotileEngine.get(), &PlacementEngineBase::windowsReleased, this,
                [this](const QStringList& windowIds, const QSet<QString>& releasedScreenIds) {
                    handleEngineWindowsReleased(m_autotileEngine.get(), windowIds, releasedScreenIds);
                });

        // ═══════════════════════════════════════════════════════════════════════════
        // Autotile Shortcut Signals
        // ═══════════════════════════════════════════════════════════════════════════

        m_autotileShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::toggleAutotileRequested,
                                                 this, &Daemon::handleTilingModeToggle);
        m_autotileShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::focusMasterRequested, this,
                                                 [this]() {
                                                     handleFocusMaster();
                                                 });
        m_autotileShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::swapWithMasterRequested,
                                                 this, [this]() {
                                                     handleSwapWithMaster();
                                                 });
        m_autotileShortcutConnections << connect(m_shortcutManager.get(),
                                                 &ShortcutManager::increaseMasterRatioRequested, this, [this]() {
                                                     handleIncreaseMasterRatio();
                                                 });
        m_autotileShortcutConnections << connect(m_shortcutManager.get(),
                                                 &ShortcutManager::decreaseMasterRatioRequested, this, [this]() {
                                                     handleDecreaseMasterRatio();
                                                 });
        m_autotileShortcutConnections << connect(m_shortcutManager.get(),
                                                 &ShortcutManager::increaseMasterCountRequested, this, [this]() {
                                                     handleIncreaseMasterCount();
                                                 });
        m_autotileShortcutConnections << connect(m_shortcutManager.get(),
                                                 &ShortcutManager::decreaseMasterCountRequested, this, [this]() {
                                                     handleDecreaseMasterCount();
                                                 });
        m_autotileShortcutConnections << connect(m_shortcutManager.get(), &ShortcutManager::retileRequested, this,
                                                 [this]() {
                                                     handleRetile();
                                                 });
    }
}

void Daemon::handleTilingModeToggle()
{
    if (!m_settings) {
        return;
    }
    if (!m_unifiedLayoutController || !m_layoutManager) {
        return;
    }
    // Feature gate happens below, after the current mode is known,
    // so we can check the flag for the TARGET mode (not just autotile).

    // Mode toggle is screen-targeted, not window-targeted: route off the
    // cursor's screen, not the focused window's. Otherwise pressing the
    // toggle while looking at vs:0 with a focused window on vs:1 silently
    // flips vs:1 — exactly the "tried to swap modes for VS0 but VS1 was
    // changing" symptom seen in production logs.
    const QString screenId = resolveCursorScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
    if (screenId.isEmpty()) {
        qCWarning(lcDaemon) << "Mode toggle: empty screenId from resolveCursorScreenId";
        return;
    }
    int desktop = currentDesktopForScreen(screenId);
    QString activity = currentActivity();
    qCInfo(lcDaemon) << "Mode toggle: screenId=" << screenId << "desktop=" << desktop << "activity=" << activity;

    // Context gate: if PlasmaZones is disabled for this screen/desktop/activity
    // in the CURRENT mode, show a visual OSD explaining why instead of silently
    // ignoring the toggle. Mode is resolved via the router so we describe the
    // mode the user is actually trying to interact with.
    // Note: intentionally shown regardless of showOsdOnLayoutSwitch — this is
    // direct feedback to an explicit user action, not a passive layout-switch OSD.
    // Fail closed on a missing resolver or engine, matching the
    // sibling gates (isFocusedContextGated and friends): a toggle
    // during the teardown window must not act ungated (the
    // wasAutotile branch below derefs the engine).
    if (!m_contextResolver || !m_autotileEngine) {
        return;
    }
    const auto currentMode = currentModeFor(screenId);
    const DisabledReason why = toDaemonDisabledReason(
        m_contextResolver->disabledReason(m_contextResolver->handleForMode(screenId, currentMode)));
    if (why != DisabledReason::NotDisabled) {
        showContextDisabledOsd(screenId, desktop, activity, why);
        return;
    }

    // Set the screen context so applyEntry knows which screen to assign
    m_unifiedLayoutController->setCurrentScreenName(screenId);

    // Temporarily include both layout types so applyLayoutById can find the
    // target across the mode boundary.  The subsequent layoutApplied /
    // autotileApplied signal will call updateLayoutFilter() and restore the
    // correct exclusive filter for the new mode.  Templates stay OUT: the
    // scrolling arm below is a direct entry write (the mode IS the
    // assignment), so no template card is ever looked up through this filter.
    m_unifiedLayoutController->setLayoutFilter(true, true, /*includeScrollingTemplates=*/false);

    QString currentAssignment = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);

    bool applied = false;
    // Three-mode cycle: Snapping → Tiling → Scrolling → Snapping.
    // currentMode is the router-resolved answer from above, so an
    // unclaimed/downgraded scrolling context cycles from Snapping,
    // matching what the user actually sees on screen.
    using Mode = PhosphorZones::AssignmentEntry::Mode;
    const auto modeEnabled = [this](Mode m) {
        switch (m) {
        case Mode::Snapping:
            return m_settings->snappingEnabled();
        case Mode::Autotile:
            return m_settings->autotileEnabled();
        case Mode::Scrolling:
            return m_settings->scrollingEnabled();
        }
        return false;
    };
    const auto nextInCycle = [](Mode m) {
        switch (m) {
        case Mode::Snapping:
            return Mode::Autotile;
        case Mode::Autotile:
            return Mode::Scrolling;
        case Mode::Scrolling:
            return Mode::Snapping;
        }
        return Mode::Snapping;
    };
    // Feature gate: only cycle INTO a mode whose master switch is
    // on. Disabled modes are skipped, so with scrolling off the
    // cycle degrades to the historical two-state flip; with every
    // other mode off the toggle is a no-op.
    Mode target = nextInCycle(currentMode);
    while (target != currentMode && !modeEnabled(target)) {
        target = nextInCycle(target);
    }
    if (target == currentMode) {
        qCInfo(lcDaemon) << "Mode toggle: ignored — no other enabled mode to cycle into";
        updateLayoutFilter();
        return;
    }
    const bool wasAutotile = (currentMode == Mode::Autotile);
    const bool wasScrolling = (currentMode == Mode::Scrolling);
    const bool toSnapping = (target == Mode::Snapping);
    qCInfo(lcDaemon) << "Mode toggle: currentAssignment=" << currentAssignment
                     << "currentMode=" << static_cast<int>(currentMode) << "target=" << static_cast<int>(target);

    // Capture autotile window order BEFORE layout switch destroys PhosphorTiles::TilingState.
    // Merge (not replace) into m_lastEngineOrders so other desktops' saved
    // orders are preserved — a replace would discard them.
    if (wasAutotile) {
        auto currentOrders = captureAutotileOrders();
        // Pre-clear ONLY the toggled screen's context: this is a
        // per-screen toggle, and dropping every active screen's
        // saved order wiped remembered orders for screens the
        // capture below may not cover (their live state can be
        // empty right now while their saved order is still the
        // re-entry seed).
        m_lastEngineOrders.remove(TilingStateKey{screenId, desktop, activity});
        for (auto it = currentOrders.constBegin(); it != currentOrders.constEnd(); ++it) {
            m_lastEngineOrders[it.key()] = it.value();
        }
    }

    if (toSnapping) {
        // → Snapping: restore this context's snappingLayout
        // from the PhosphorZones::AssignmentEntry (preserved even when mode is Autotile).
        // Try current context first; fall back to broader scopes when the
        // context-specific entry has no snappingLayout (e.g., fresh KCM
        // autotile entry that never had a manual layout assigned).
        QString layoutId = m_layoutManager->snappingLayoutForScreen(screenId, desktop, activity);
        if (layoutId.isEmpty() && !activity.isEmpty()) {
            layoutId = m_layoutManager->snappingLayoutForScreen(screenId, desktop, QString());
        }
        if (!layoutId.isEmpty()) {
            applied = m_unifiedLayoutController->applyLayoutById(layoutId);
        }
        // Fallback when snappingLayout is empty (fresh install) or stale
        // (layout was deleted). Use the active layout or first available layout.
        if (!applied) {
            PhosphorZones::Layout* fallback = m_layoutManager->activeLayout();
            if (!fallback && !m_layoutManager->layouts().isEmpty()) {
                fallback = m_layoutManager->layouts().first();
            }
            if (fallback) {
                applied = m_unifiedLayoutController->applyLayoutById(fallback->id().toString());
            }
        }
    } else if (target == Mode::Autotile) {
        // Pre-save snap-float state before autotile entry so rapid
        // toggles don't lose snap-mode floats. Scoped to the toggled
        // screen — windows floating on other screens are unaffected.
        presaveSnapFloats(screenId);

        // Pre-seed autotile engine with saved autotile order (if available)
        // or zone-ordered windows. Only seed the focused screen — the toggle
        // is per-screen, not global.
        seedAutotileOrderForScreen(screenId);

        // Resolve algorithm from the AssignmentEntry's tilingAlgorithm
        // (preserved even when mode is Snapping), then fall back to broader
        // scopes. This is the EXPLICITLY-assigned algorithm for the context.
        QString algoId = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity);
        if (algoId.isEmpty() && !activity.isEmpty()) {
            algoId = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, QString());
        }
        // No explicitly-assigned algorithm: fall back to the user's
        // configured default — UNLESS the default is suppressed for this
        // context. Under suppress, switching to autotile applies a bare
        // "autotile:" assignment (mode set, no algorithm) so the context
        // selects autotile mode but does NOT tile with the global default;
        // updateEngineScreens skips a suppressed bare context until the
        // user assigns a concrete algorithm.
        if (algoId.isEmpty()
            && !m_layoutManager->isDefaultAssignmentSuppressedForContext(screenId, desktop, activity)) {
            if (m_settings) {
                algoId = m_settings->defaultAutotileAlgorithm();
            }
            if (algoId.isEmpty()) {
                algoId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
            }
        }
        if (!algoId.isEmpty()) {
            applied = m_unifiedLayoutController->applyLayoutById(PhosphorLayout::LayoutId::makeAutotileId(algoId));
        } else {
            // Suppressed with no explicitly-assigned algorithm: switch the
            // context to autotile mode WITHOUT an algorithm so it selects the
            // mode but does not tile. applyLayoutById can't apply a bare
            // "autotile:" (it has no matching layout preview and returns
            // false), so write the entry directly. The emitted layoutAssigned
            // drives the daemon's updateEngineScreens (which skips this bare
            // suppressed context, so it does not tile) AND updateLayoutFilter,
            // so the mode filter refreshes off that signal — no explicit call
            // needed here.
            // Seed from the stored entry so the slots this arm does not name
            // (snapping layout, scrolling template) survive the flip:
            // carryOverNonAssignmentActions excludes SetScrollingTemplate, so a
            // fresh entry would drop the template outright. Same seeding shape
            // LayoutAdaptor::setAssignmentEntry uses. The entry stays "bare"
            // regardless — this arm is only reached when the resolved algorithm
            // is empty, so the seeded tilingAlgorithm is empty too.
            PhosphorZones::AssignmentEntry entry = m_layoutManager->exactContextEntry(screenId, desktop, activity);
            entry.mode = PhosphorZones::AssignmentEntry::Autotile;
            m_layoutManager->setAssignmentEntryDirect(screenId, desktop, activity, entry);
            // No layoutApplied/autotileApplied signal fires for a direct
            // entry write, so surface the feedback OSD here: the mode
            // switched to autotile but nothing is assigned to tile with.
            // The cheatsheet refilter rides those same signals, so an
            // open sheet also needs the explicit nudge on this path.
            //
            // DELIBERATELY ungated on showOsdOnLayoutSwitch, unlike the
            // scrolling arm's mode card below. That toggle governs passive
            // "your layout changed" announcements; this card explains why an
            // explicit user action produced no visible effect, which is the
            // same job the locked-context card does — and that one bypasses
            // the toggle for the same reason.
            showNotAssignedOsd(screenId);
            refreshCheatsheetIfVisible();
            // A snap-assist popup is stale the moment placement changes; the
            // two signal-driven arms and the scrolling arm all hide it, and
            // this arm's direct write reaches none of them.
            if (m_overlayService) {
                m_overlayService->hideSnapAssist();
            }
            applied = true;
        }
    } else {
        // → Scrolling. The mode IS the assignment (the strip has
        // no layout entity), so like the bare-autotile arm this is
        // a direct entry write — applyLayoutById has nothing to
        // apply. Both sibling slots are carried so the toggle
        // stays lossless: cycling back restores the same snapping
        // layout and tiling algorithm this context had before.
        // Snap-float presave for a snapping→scrolling flip runs in
        // updateEngineScreens' derive pass (driven by the emitted
        // layoutAssigned), which fires before the scroll engine
        // claims the screen.
        // Seeded from the stored entry, not built fresh: the scrolling
        // template lives in a slot this arm never names, and
        // carryOverNonAssignmentActions excludes SetScrollingTemplate, so a
        // fresh entry would wipe the template on the way INTO the mode that
        // consumes it. Same seeding shape LayoutAdaptor::setAssignmentEntry
        // uses. The two sibling slots below are still re-resolved because their
        // resolvers widen past the exact context (empty-activity fallback).
        PhosphorZones::AssignmentEntry entry = m_layoutManager->exactContextEntry(screenId, desktop, activity);
        entry.mode = Mode::Scrolling;
        entry.snappingLayout = m_layoutManager->snappingLayoutForScreen(screenId, desktop, activity);
        if (entry.snappingLayout.isEmpty() && !activity.isEmpty()) {
            entry.snappingLayout = m_layoutManager->snappingLayoutForScreen(screenId, desktop, QString());
        }
        entry.tilingAlgorithm = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity);
        if (entry.tilingAlgorithm.isEmpty() && !activity.isEmpty()) {
            entry.tilingAlgorithm = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, QString());
        }
        m_layoutManager->setAssignmentEntryDirect(screenId, desktop, activity, entry);
        // Direct writes fire no layoutApplied/autotileApplied, so
        // nudge every surface those signals normally refresh.
        refreshCheatsheetIfVisible();
        // Announce the mode change. Both sibling arms give feedback
        // (layoutApplied → Snapping, autotileApplied → Autotile, and
        // showNotAssignedOsd for the bare-autotile direct write), so
        // without this the primary way into scrolling was the only
        // silent one. showScrollingModeOsd exists for exactly this
        // announcement and was reachable only from the KCM apply path
        // and the desktop-switch OSD.
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            showScrollingModeOsd(screenId, OsdTrigger::LayoutSwitch);
        }
        // A snap-assist popup is stale the moment placement changes;
        // both signal-driven arms hide it for that reason.
        if (m_overlayService) {
            m_overlayService->hideSnapAssist();
        }
        applied = true;
    }

    // If apply failed (e.g. layout was deleted), restore the correct filter
    if (!applied) {
        updateLayoutFilter();
    }

    // Resnap autotiled windows into the new manual layout's zones using
    // autotile window order. resnapFromAutotileOrder maps window N to zone N,
    // giving a deterministic layout-fill. resnapCurrentAssignments can't be
    // used here because zone assignments are keyed by window ID globally —
    // it would find zone assignments from OTHER desktops' windows that happen
    // to share the same screen, producing wrong results.
    // Windows that were autotile-only (never zone-snapped) get their
    // pre-autotile floating geometry restored via the batched buildAutotileRestoreEntries →
    // emitBatchedResnap path.
    auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get());
    if (wasAutotile && !toSnapping) {
        // Autotile → Scrolling: the released windows re-enter the
        // strip, so replaying the preserved snap-ZONE half would
        // fight the scroll engine's placement. Drop it; the
        // windows' snap state stays in the unified record for a
        // later scrolling→snapping flip to restore.
        m_pendingSnapFloatRestores.clear();
    } else if (wasScrolling) {
        // Scrolling → anywhere. The scroll release inside
        // applyLayoutById reaches handleEngineWindowsReleased, which
        // appends snap-ZONE entries that the updateEngineScreens tail
        // drain deliberately PRESERVES for "the mode-toggle consumer".
        // Every consumer below is gated on wasAutotile, so that
        // consumer never ran: the batch simply survived until the next
        // recompute's clear discarded it, and windows snapped to zones
        // before the screen entered scrolling never returned to those
        // zones. Resolve it here. To Snapping, the buffer resnap below
        // is what puts the windows back on their zones (it reads each
        // one's durable snap slot); the drain that follows releases the
        // FLOAT half, since a full consume discards the zone entries.
        // To Autotile it drops them for the same reason the
        // autotile→scrolling arm above does (the strip/stack owns
        // placement, and the snap state stays in the unified record).
        if (toSnapping) {
            // Buffer-based resnap into the restored snapping layout,
            // scoped to the toggled screen and current desktop. This
            // used to happen only as a side effect of the rulesChanged
            // reconcile re-entering the KCM apply path mid-toggle;
            // with that reconcile deferred and self-write-suppressed,
            // this branch must drive its own resnap or the released
            // strip windows keep their scroll geometry. Mirrors
            // resnapIfManualMode (navigation.cpp) including the
            // engine-managed exclude union and the OSD suppress arm.
            // The service() check is redundant defence, not a real
            // guard: WindowTrackingAdaptor assigns m_service in its
            // constructor and never nulls it, so a live adaptor always
            // has one. It is kept because the body dereferences it and
            // the invariant lives in another class's header, but do not
            // read it as evidence that a null service is reachable —
            // most daemon sites check only the adaptor, and they are
            // right to.
            if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service() && m_snapAdaptor) {
                QSet<QString> engineManagedScreens;
                if (m_screenModeRouter && m_screenManager) {
                    const auto parts = m_screenModeRouter->partitionByMode(m_screenManager->effectiveScreenIds());
                    engineManagedScreens = QSet<QString>(parts.autotile.begin(), parts.autotile.end());
                    engineManagedScreens.unite(QSet<QString>(parts.scrolling.begin(), parts.scrolling.end()));
                }
                // The toggled screen's OWN desktop, not the global
                // current one: under per-output virtual desktops
                // (#648) the two differ, and this path knows which
                // screen was toggled. The KCM-apply sibling
                // (init_assignment_apply.cpp) still passes the global
                // current desktop because one call covers several
                // screens at once and the parameter takes a single
                // value; the per-window filter inside the service
                // is what keeps that case correct.
                m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(engineManagedScreens, {screenId},
                                                                                      desktop);
                armResnapOsdSuppression(1);
                m_snapAdaptor->resnapToNewLayout();
            }
            emitPendingSnapFloatRestoresForResnapBuffer();
        } else {
            m_pendingSnapFloatRestores.clear();
        }
    } else if (wasAutotile && (!applied || !concreteSnap)) {
        if (applied && m_snapEngine) {
            qCWarning(lcDaemon) << "Snap engine is not a SnapEngine — autotile→snap resnap skipped";
        }
        // The resnap path below is what normally consumes the
        // snap-ZONE half that the updateEngineScreens tail drain
        // preserved for us. Bailing without draining leaves it to be
        // wiped by the next recompute's clear, or replayed by an
        // unrelated later consumer — so drop the preserved zone half
        // here. No restoration is attempted: this arm is reached
        // either because the layout apply failed (there is no target
        // layout to snap into) or because there is no SnapEngine to
        // apply the entries at all.
        emitPendingSnapFloatRestoresForResnapBuffer();
    } else if (applied && wasAutotile && concreteSnap) {
        // Build exclusion set: windows that fit into the target layout's zones
        // will be zone-snapped by the resnap D-Bus signal. Without excluding them,
        // buildAutotileRestoreEntries + emitBatchedResnap send float-geometry D-Bus calls that
        // arrive AFTER the resnap and overwrite the zone positions.
        // Use per-screen zone count (not global activeLayout) because each screen
        // may have a different layout assigned with a different zone count.
        // Only process entries for the CURRENT desktop (m_lastEngineOrders
        // accumulates entries across desktops after the merge fix).
        //
        // Batch all resnap entries into ONE signal to eliminate the race condition
        // where multiple per-screen signals cause the effect to restore geometries
        // and resnap in parallel.
        PhosphorPlacement::WindowTrackingService* wts =
            m_windowTrackingAdaptor ? m_windowTrackingAdaptor->service() : nullptr;
        QSet<QString> resnappedWindows;
        QVector<ZoneAssignmentEntry> allResnapEntries;

        // Zones already reserved by the windowsReleased snap-zone restores
        // (branch b — windows floated in autotile, which are absent from the
        // tile order). Feed them to the order-resnap so its positional
        // fallback never re-uses a reserved zone — the two-windows-one-zone
        // collision. Also mark those windows as resnapped so
        // buildAutotileRestoreEntries doesn't additionally emit a pre-tile
        // geometry restore for them.
        QStringList preClaimedZoneIds;
        for (const ZoneAssignmentEntry& e : m_pendingSnapFloatRestores) {
            // The RestoreSentinel arm is unreachable today — the
            // updateEngineScreens tail drain emitted every float
            // entry and left only zone entries behind. Kept as
            // defence so a future drain-mode change cannot silently
            // turn a float restore into a pre-claimed zone id.
            if (e.targetZoneId.isEmpty() || e.targetZoneId == RestoreSentinel) {
                continue; // float restore (no zone claimed)
            }
            if (e.targetZoneIds.isEmpty()) {
                preClaimedZoneIds.append(e.targetZoneId);
            } else {
                preClaimedZoneIds.append(e.targetZoneIds);
            }
            resnappedWindows.insert(e.windowId);
        }

        for (auto it = m_lastEngineOrders.constBegin(); it != m_lastEngineOrders.constEnd(); ++it) {
            if (it.key().desktop != desktop || it.key().activity != activity) {
                continue;
            }
            // Only resnap the focused screen — the toggle is per-screen
            if (it.key().screenId != screenId) {
                continue;
            }
            // Both per-window filters below need the tracking service to
            // judge a window, and both are written as `wts && …`, so a null
            // service passes EVERY window through — the anti-conservative
            // direction. An unjudgeable window must not be resnapped onto a
            // zone; skip the order-resnap entirely and let
            // buildAutotileRestoreEntries below hand these windows their
            // pre-tile geometry instead.
            if (!wts) {
                continue;
            }
            const QStringList& fullOrder = it.value();
            const QString& resnapScreenId = it.key().screenId;

            // Filter out floating windows — windowsReleased already
            // restored their snap-float state. Resnapping them would override
            // the restored float with a zone snap.
            // Also skip windows that were never snapped — they get pre-autotile
            // geometry via buildAutotileRestoreEntries + emitBatchedResnap.
            // "Never snapped" is judged by recordedSnapZones (live assignment
            // OR the durable record snap slot), NOT the live cache alone: after
            // a daemon restart the live cache is cold, so a live-only check
            // would treat every previously-snapped window as never-snapped and
            // float-restore it instead of resnapping to its zone.
            QStringList windowOrder;
            for (const QString& windowId : fullOrder) {
                // Minimize exception, mirroring the seed filter: a minimized
                // window reads as floating (suspension float), but it must
                // still receive its resnap entry or it unminimizes at the
                // stale autotile rect before anything places it.
                const bool minimized =
                    wts->windowRegistry() && wts->windowRegistry()->minimizedState(windowId).value_or(false);
                if (wts->isWindowFloating(windowId) && !minimized) {
                    continue;
                }
                if (wts->recordedSnapZones(windowId).isEmpty()) {
                    continue;
                }
                windowOrder.append(windowId);
            }

            // Use the raw, no-fallback calculation — NOT
            // calculateResnapEntriesFromAutotileOrder, whose "empty order ⇒
            // resnap from current assignments" fallback is the global-keyed
            // current-assignment resnap the comment above forbids here. When
            // windowOrder is empty (every window was never snapped, so the
            // filter above dropped them all), that fallback returns the
            // windows' stale current zone assignments — their tiled geometry —
            // which both re-pins them to the tile positions AND marks them as
            // resnapped, suppressing the intended pre-tile float-back in
            // buildAutotileRestoreEntries. The raw call returns nothing for an
            // empty order, letting those windows correctly float back.
            QVector<ZoneAssignmentEntry> entries =
                concreteSnap->calculateResnapFromAutotileOrder(windowOrder, resnapScreenId, preClaimedZoneIds);
            // Derive the exclusion set from the entries actually produced —
            // not a min(windows, zoneCount) guess. With zones pre-claimed by
            // branch-b restores, fewer windows get a zone; a window that got
            // none must remain eligible for buildAutotileRestoreEntries (its
            // pre-tile geometry) rather than being silently dropped.
            for (const ZoneAssignmentEntry& e : entries) {
                resnappedWindows.insert(e.windowId);
            }
            allResnapEntries.append(entries);
        }
        // Batch the remaining restore entries into the resnap signal:
        // 1. Snap-ZONE restores collected during windowsReleased (the
        //    float half was already emitted by the updateEngineScreens
        //    tail drain, which preserves only the zone half for us)
        // 2. Autotile-only windows (never zone-snapped, need pre-tile geometry)
        // This eliminates individual D-Bus signals that would queue behind
        // the resnap, causing visible delay for floating/new windows.
        allResnapEntries.append(m_pendingSnapFloatRestores);
        m_pendingSnapFloatRestores.clear();
        QVector<ZoneAssignmentEntry> restoreEntries =
            buildAutotileRestoreEntries(resnappedWindows, desktop, activity, screenId);
        allResnapEntries.append(restoreEntries);

        // Emit ONE batched signal (suppresses one OSD regardless of screen count)
        concreteSnap->emitBatchedResnap(allResnapEntries);
        // Empty ⇒ no feedback to suppress; a no-op arm must NOT zero a
        // concurrent stream's outstanding count (the old `= 0` did).
        armResnapOsdSuppression(allResnapEntries.isEmpty() ? 0 : 1);
    }
}

} // namespace PlasmaZones
