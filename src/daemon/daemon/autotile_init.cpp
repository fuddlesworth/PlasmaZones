// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Daemon::initializeAutotile() — the autotile engine's signal wiring and its
// eight shortcut handlers. Split out of daemon/signals.cpp along the seam
// docs/file-size-remediation.md names for that file; the rest of the daemon's
// signal wiring stays there.

#include "daemon/daemon.h"
#include "helpers.h"
#include "config/settings.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/overlayservice.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include <PhosphorEngine/PlacementEngineBase.h>
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
                    // Clear unconditionally: entries from a previous release
                    // batch are stale the moment a new one arrives, and leaving
                    // them behind because the adaptor happens to be null would
                    // leak them into the next toggle.
                    m_pendingSnapFloatRestores.clear();
                    if (m_windowTrackingAdaptor) {
                        PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
                        for (const QString& windowId : windowIds) {
                            // Only process windows whose current WTS screen is one of the
                            // screens being released. A window that moved to a different
                            // screen (e.g., dragged from autotile VS to snap VS and resnapped)
                            // is no longer on the releasing screen — its state on the current
                            // screen must not be disturbed.
                            const QString windowScreen = wts->screenForWindow(windowId);
                            if (!windowScreen.isEmpty() && !releasedScreenIds.contains(windowScreen)) {
                                // Window is on a different screen — do NOT touch its state.
                                // It may be on another autotile screen (flag still valid) or
                                // a snap screen (flag already cleared by assignWindowToZones).
                                qCDebug(lcDaemon) << "windowsReleased: skipping" << windowId << "on screen"
                                                  << windowScreen << "(not in released set)";
                                continue;
                            }
                            if (!m_autotileEngine || !m_snapEngine)
                                continue;
                            // Clear autotile-originated floats (they don't persist into snap mode)
                            bool wasAutotileFloated = m_autotileEngine->isModeSpecificFloated(windowId);
                            if (wasAutotileFloated) {
                                m_windowTrackingAdaptor->setWindowFloating(windowId, false);
                            }
                            m_autotileEngine->clearModeSpecificFloatMarker(windowId);
                            // Restore the snap-mode float from the SINGLE source of truth — the
                            // window's placement record (its snap slot), captured when the screen
                            // last left snapping. No parallel saved-float set. Float state is set
                            // immediately; geometry restore is deferred to the batched resnap
                            // signal to avoid individual D-Bus signals queuing behind the resnap.
                            // Exact record only: this is a LIVE mid-session window (uuids stable),
                            // so a same-app sibling's record must not float/zone-restore it.
                            const auto rec = wts->placementStore().peekExact(windowId);
                            const PhosphorEngine::EngineSlot snapSlot = rec
                                ? rec->slotFor(PhosphorEngine::WindowPlacement::snapEngineId())
                                : PhosphorEngine::EngineSlot{};
                            const bool snapFloat = snapSlot.state == PhosphorEngine::WindowPlacement::stateFloating();
                            // A window SNAPPED in snapping mode, then floated in autotile, keeps its
                            // snap-engine state — float is PER ENGINE. Such a window is excluded from
                            // the captured autotile tile order (floated windows aren't ordered), so the
                            // order-driven resnap and buildAutotileRestoreEntries never see it; without
                            // this branch it falls through every restore path and keeps its autotile-
                            // float geometry on return to snapping (the "still floated" bug). Gated on
                            // wasAutotileFloated so order-driven (tiled, non-floated) windows — which the
                            // order-resnap path already handles — are not double-snapped here.
                            const bool snapSnapped = wasAutotileFloated
                                && snapSlot.state == PhosphorEngine::WindowPlacement::stateSnapped()
                                && !snapSlot.zoneIds.isEmpty();
                            if (snapFloat) {
                                qCInfo(lcDaemon) << "windowsReleased: restoring snap-float for" << windowId;
                                m_windowTrackingAdaptor->setWindowFloating(windowId, true);
                                const QString screen = wts->screenForWindow(windowId);
                                QRect g = rec->freeGeometryFor(screen.isEmpty() ? rec->screenId : screen);
                                if (!g.isValid()) {
                                    g = rec->anyFreeGeometry();
                                }
                                if (g.isValid()) {
                                    ZoneAssignmentEntry entry;
                                    entry.windowId = windowId;
                                    entry.targetZoneId = RestoreSentinel;
                                    entry.targetGeometry = g;
                                    m_pendingSnapFloatRestores.append(entry);
                                }
                            } else if (snapSnapped) {
                                const QString screen = wts->screenForWindow(windowId);
                                const QString restoreScreen = screen.isEmpty() ? rec->screenId : screen;
                                const QRect geo = wts->resolveZoneGeometry(snapSlot.zoneIds, restoreScreen);
                                if (geo.isValid()) {
                                    qCInfo(lcDaemon) << "windowsReleased: restoring snap-zone for" << windowId
                                                     << "zones=" << snapSlot.zoneIds << "screen=" << restoreScreen;
                                    // Float is already cleared above for autotile-floated windows; this
                                    // window returns to its snapped state, not floating. Multiple windows
                                    // may legitimately share a zone, so no cross-window zone dedup here.
                                    ZoneAssignmentEntry entry;
                                    entry.windowId = windowId;
                                    entry.targetZoneId = snapSlot.zoneIds.first();
                                    entry.targetZoneIds = snapSlot.zoneIds;
                                    entry.targetGeometry = geo;
                                    entry.targetScreenId = restoreScreen;
                                    // The durable record knows which desktop this snap
                                    // belongs to; carry it so the batch commit doesn't
                                    // re-stamp the window onto the current desktop.
                                    entry.virtualDesktop = rec->virtualDesktop;
                                    m_pendingSnapFloatRestores.append(entry);
                                } else {
                                    qCWarning(lcDaemon) << "windowsReleased: snap-zone restore for" << windowId
                                                        << "failed — zone geometry unresolved for" << snapSlot.zoneIds;
                                }
                            } else {
                                qCDebug(lcDaemon) << "windowsReleased: no snap-float to restore for" << windowId
                                                  << "wasAutotileFloated:" << wasAutotileFloated;
                            }
                        }
                    }
                });

        // ═══════════════════════════════════════════════════════════════════════════
        // Autotile Shortcut Signals
        // ═══════════════════════════════════════════════════════════════════════════

        m_autotileShortcutConnections << connect(
            m_shortcutManager.get(), &ShortcutManager::toggleAutotileRequested, this, [this]() {
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
                qCInfo(lcDaemon) << "Mode toggle: screenId=" << screenId << "desktop=" << desktop
                                 << "activity=" << activity;

                // Context gate: if PlasmaZones is disabled for this screen/desktop/activity
                // in the CURRENT mode, show a visual OSD explaining why instead of silently
                // ignoring the toggle. Mode is resolved via the router so we describe the
                // mode the user is actually trying to interact with.
                // Note: intentionally shown regardless of showOsdOnLayoutSwitch — this is
                // direct feedback to an explicit user action, not a passive layout-switch OSD.
                const auto currentMode = currentModeFor(screenId);
                // Legacy direct settings check — kept inline because the OSD
                // surface needs the rich PlasmaZones::DisabledReason enum
                // (which carries axis info for the user-facing message);
                // PhosphorContext::DisabledReason in the LGPL lib is a
                // narrower projection. Migrating this site requires a richer
                // resolver API and is tracked as a follow-up.
                const DisabledReason why =
                    contextDisabledReason(m_settings.get(), currentMode, screenId, desktop, activity);
                if (why != DisabledReason::NotDisabled) {
                    showContextDisabledOsd(screenId, desktop, activity, why);
                    return;
                }

                // Set the screen context so applyEntry knows which screen to assign
                m_unifiedLayoutController->setCurrentScreenName(screenId);

                // Temporarily include both layout types so applyLayoutById can find the
                // target across the mode boundary.  The subsequent layoutApplied /
                // autotileApplied signal will call updateLayoutFilter() and restore the
                // correct exclusive filter for the new mode.
                m_unifiedLayoutController->setLayoutFilter(true, true);

                QString currentAssignment = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);

                bool applied = false;
                const bool wasAutotile = PhosphorLayout::LayoutId::isAutotile(currentAssignment);
                qCInfo(lcDaemon) << "Mode toggle: currentAssignment=" << currentAssignment
                                 << "wasAutotile=" << wasAutotile;

                // Feature gate: only allow toggling INTO a mode whose feature flag is enabled.
                // Without this, disabling snapping in the KCM while autotile remains on still
                // lets the user toggle back into snapping — and vice versa.
                const bool targetAutotile = !wasAutotile;
                if (targetAutotile && !m_settings->autotileEnabled()) {
                    qCInfo(lcDaemon) << "Mode toggle: ignored — autotile disabled in settings";
                    updateLayoutFilter();
                    return;
                }
                if (!targetAutotile && !m_settings->snappingEnabled()) {
                    qCInfo(lcDaemon) << "Mode toggle: ignored — snapping disabled in settings";
                    updateLayoutFilter();
                    return;
                }

                // Capture autotile window order BEFORE layout switch destroys PhosphorTiles::TilingState.
                // Merge (not replace) into m_lastAutotileOrders so other desktops' saved
                // orders are preserved — a replace would discard them.
                if (wasAutotile) {
                    auto currentOrders = captureAutotileOrders();
                    for (auto it = currentOrders.constBegin(); it != currentOrders.constEnd(); ++it) {
                        m_lastAutotileOrders[it.key()] = it.value();
                    }
                }

                if (wasAutotile) {
                    // Autotile → Snapping: restore this context's snappingLayout
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
                } else {
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
                    // updateAutotileScreens skips a suppressed bare context until the
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
                        applied = m_unifiedLayoutController->applyLayoutById(
                            PhosphorLayout::LayoutId::makeAutotileId(algoId));
                    } else {
                        // Suppressed with no explicitly-assigned algorithm: switch the
                        // context to autotile mode WITHOUT an algorithm so it selects the
                        // mode but does not tile. applyLayoutById can't apply a bare
                        // "autotile:" (it has no matching layout preview and returns
                        // false), so write the entry directly. The emitted layoutAssigned
                        // drives the daemon's updateAutotileScreens (which skips this bare
                        // suppressed context, so it does not tile) AND updateLayoutFilter,
                        // so the mode filter refreshes off that signal — no explicit call
                        // needed here.
                        PhosphorZones::AssignmentEntry entry;
                        entry.mode = PhosphorZones::AssignmentEntry::Autotile;
                        m_layoutManager->setAssignmentEntryDirect(screenId, desktop, activity, entry);
                        // No layoutApplied/autotileApplied signal fires for a direct
                        // entry write, so surface the feedback OSD here: the mode
                        // switched to autotile but nothing is assigned to tile with.
                        // The cheatsheet refilter rides those same signals, so an
                        // open sheet also needs the explicit nudge on this path.
                        showNotAssignedOsd(screenId);
                        refreshCheatsheetIfVisible();
                        applied = true;
                    }
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
                if (wasAutotile && (!applied || !concreteSnap)) {
                    if (applied && m_snapEngine) {
                        qCWarning(lcDaemon) << "Snap engine is not a SnapEngine — autotile→snap resnap skipped";
                    }
                    // The resnap path below is what normally consumes
                    // m_pendingSnapFloatRestores. Bailing without draining leaves
                    // them to be wiped by the next windowsReleased clear(), so the
                    // user's snap-mode floats never come back — emit them here.
                    //
                    // NOTE this helper emits only the float half and drops the
                    // snap-zone entries, on the assumption that an in-flight
                    // resnap consumes those. No resnap runs on THIS bail, so the
                    // zone entries are dropped deliberately: without a SnapEngine
                    // there is nothing that could apply them.
                    emitPendingSnapFloatRestoresForResnapBuffer();
                } else if (applied && wasAutotile && concreteSnap) {
                    // Build exclusion set: windows that fit into the target layout's zones
                    // will be zone-snapped by the resnap D-Bus signal. Without excluding them,
                    // buildAutotileRestoreEntries + emitBatchedResnap send float-geometry D-Bus calls that
                    // arrive AFTER the resnap and overwrite the zone positions.
                    // Use per-screen zone count (not global activeLayout) because each screen
                    // may have a different layout assigned with a different zone count.
                    // Only process entries for the CURRENT desktop (m_lastAutotileOrders
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

                    for (auto it = m_lastAutotileOrders.constBegin(); it != m_lastAutotileOrders.constEnd(); ++it) {
                        if (it.key().desktop != desktop || it.key().activity != activity) {
                            continue;
                        }
                        // Only resnap the focused screen — the toggle is per-screen
                        if (it.key().screenId != screenId) {
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
                            if (wts && wts->isWindowFloating(windowId)) {
                                continue;
                            }
                            if (wts && wts->recordedSnapZones(windowId).isEmpty()) {
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
                        QVector<ZoneAssignmentEntry> entries = concreteSnap->calculateResnapFromAutotileOrder(
                            windowOrder, resnapScreenId, preClaimedZoneIds);
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
                    // Batch float-restore entries into the resnap signal:
                    // 1. Snap-float restores (collected during windowsReleased)
                    // 2. Autotile-only windows (never zone-snapped, need pre-tile geometry)
                    // This eliminates individual D-Bus signals that would queue behind
                    // the resnap, causing visible delay for floating/new windows.
                    allResnapEntries.append(m_pendingSnapFloatRestores);
                    m_pendingSnapFloatRestores.clear();
                    QVector<ZoneAssignmentEntry> restoreEntries =
                        buildAutotileRestoreEntries(resnappedWindows, desktop, activity);
                    allResnapEntries.append(restoreEntries);

                    // Emit ONE batched signal (suppresses one OSD regardless of screen count)
                    concreteSnap->emitBatchedResnap(allResnapEntries);
                    // Empty ⇒ no feedback to suppress; a no-op arm must NOT zero a
                    // concurrent stream's outstanding count (the old `= 0` did).
                    armResnapOsdSuppression(allResnapEntries.isEmpty() ? 0 : 1);
                }
            });

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

} // namespace PlasmaZones
