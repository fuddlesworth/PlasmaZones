// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../shortcutmanager.h"
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/utils.h"
#include <PhosphorPlacement/WindowTrackingService.h>
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/zonedetectionadaptor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include "../../config/settings.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

using PlacementEngineBase = PhosphorEngine::PlacementEngineBase;

namespace PlasmaZones {

void Daemon::initializeAutotile()
{
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
                    if (m_windowTrackingAdaptor) {
                        PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
                        m_pendingSnapFloatRestores.clear();
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

        connect(m_shortcutManager.get(), &ShortcutManager::toggleAutotileRequested, this, [this]() {
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
            qCInfo(lcDaemon) << "Mode toggle: currentAssignment=" << currentAssignment << "wasAutotile=" << wasAutotile;

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
                    applied =
                        m_unifiedLayoutController->applyLayoutById(PhosphorLayout::LayoutId::makeAutotileId(algoId));
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
                    showNotAssignedOsd(screenId);
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
            // pre-autotile floating geometry restored via the batched buildAutotileRestoreEntries → emitBatchedResnap
            // path.
            auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get());
            if (applied && wasAutotile && !concreteSnap) {
                if (m_snapEngine) {
                    qCWarning(lcDaemon) << "Snap engine is not a SnapEngine — autotile→snap resnap skipped";
                }
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

        connect(m_shortcutManager.get(), &ShortcutManager::focusMasterRequested, this, [this]() {
            handleFocusMaster();
        });
        connect(m_shortcutManager.get(), &ShortcutManager::swapWithMasterRequested, this, [this]() {
            handleSwapWithMaster();
        });
        connect(m_shortcutManager.get(), &ShortcutManager::increaseMasterRatioRequested, this, [this]() {
            handleIncreaseMasterRatio();
        });
        connect(m_shortcutManager.get(), &ShortcutManager::decreaseMasterRatioRequested, this, [this]() {
            handleDecreaseMasterRatio();
        });
        connect(m_shortcutManager.get(), &ShortcutManager::increaseMasterCountRequested, this, [this]() {
            handleIncreaseMasterCount();
        });
        connect(m_shortcutManager.get(), &ShortcutManager::decreaseMasterCountRequested, this, [this]() {
            handleDecreaseMasterCount();
        });
        connect(m_shortcutManager.get(), &ShortcutManager::retileRequested, this, [this]() {
            handleRetile();
        });
    }
}

void Daemon::initializeUnifiedController()
{
    // Initialize unified layout controller (manual layouts only).
    // Registry injected explicitly (not reached via engine->algorithmRegistry()):
    // keeps DI contract visible at the call site and lets unit tests stub the
    // engine without losing algorithm enumeration.
    m_unifiedLayoutController =
        std::make_unique<UnifiedLayoutController>(m_layoutManager.get(), m_settings.get(), m_screenManager.get(),
                                                  m_algorithmRegistry.get(), m_autotileEngine.get(), this);

    // Share the daemon's bundle-owned autotile source with the controller
    // so its internal layout cache's autotile half is populated from the
    // bundle's preview cache instead of rebuilt from scratch per call.
    m_unifiedLayoutController->setAutotileLayoutSource(m_autotileLayoutSource);

    // Set initial desktop/activity context for visibility-filtered cycling
    const int desktopNow = currentDesktop();
    m_layoutManager->setCurrentVirtualDesktop(desktopNow);
    m_unifiedLayoutController->setCurrentVirtualDesktop(desktopNow);
    if (m_activityManager && PhosphorWorkspaces::ActivityManager::isAvailable()) {
        m_layoutManager->setCurrentActivity(m_activityManager->currentActivity());
        m_unifiedLayoutController->setCurrentActivity(m_activityManager->currentActivity());
    }
}

void Daemon::connectLayoutSignals()
{
    // ═══════════════════════════════════════════════════════════════════════════
    // Mode-based layout filtering
    // ═══════════════════════════════════════════════════════════════════════════

    // Derive initial per-screen autotile state from assignments
    updateAutotileScreens();

    // Set initial layout filter
    updateLayoutFilter();

    // Pre-warm the per-screen unified passive overlay shell. The shell
    // hosts every kbd-None overlay slot (OSD, snap-assist, …) so a
    // single warm-up at daemon start covers QML compile + first-paint
    // pipeline build for all of them — subsequent per-content shows are
    // animator-only operations on already-mapped slot Items inside the
    // already-warmed shell wl_surface. Deferred so daemon init completes
    // first.
    if (m_overlayService) {
        QTimer::singleShot(0, this, [this]() {
            m_overlayService->warmUpNotifications();
            // Post-shell-migration every kbd-None overlay (OSD,
            // snap-assist, layout picker, zone-selector, main overlay)
            // lives as an Item slot inside the per-screen passive shell.
            // warmUpNotifications builds the shell wl_surface +
            // QQuickWindow + QML compile + first-paint pipeline once per
            // screen, which covers QML compile cost for every slot. Each
            // slot's content body (SnapAssistContent / LayoutPickerContent
            // / ZoneSelectorContent) is async-loaded on its first show
            // via the slot's `loaded` toggle, so per-content classes only
            // pay their construction cost when the user actually summons
            // them rather than at daemon start.
        });
    }

    // Note: autotileEnabledChanged, snappingEnabledChanged, and perScreenAutotileSettingsChanged
    // are handled by the consolidated settingsChanged handler (lives in
    // daemon.cpp since the daemon file split — single-pass processing).
    // Individual autotile signals only fire from runtime setters, where settingsChanged also
    // fires and handles the transitions — no separate handlers needed.

    // Re-derive autotile screens and layout filter when assignments change
    // (e.g., from D-Bus, layout picker, unified controller).
    // Also sync the unified controller's cycling index when the assignment
    // affects the current desktop — needed for D-Bus/KCM batch operations.
    // Do NOT touch setActiveLayout here — applyEntry and the drop-time
    // cross-layout switch in WindowDragAdaptor (drop.cpp) handle active
    // layout themselves, and calling it here with QSignalBlocker steals
    // the activeLayoutChanged transition, leaving the resnap buffer
    // empty. Desktop switches sync active layout via syncModeFromAssignments().
    connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutAssigned, this,
            [this](const QString& screenId, int virtualDesktop, PhosphorZones::Layout* /*layout*/) {
                updateAutotileScreens();
                updateLayoutFilter();

                // Sync unified controller cycling index when assignment affects current desktop.
                const int curDesktop = currentDesktopForScreen(screenId);
                if (virtualDesktop != 0 && virtualDesktop != curDesktop) {
                    return;
                }
                if (!m_unifiedLayoutController || !m_layoutManager) {
                    return;
                }
                const QString focusedScreenId = m_unifiedLayoutController->currentScreenName();
                if (focusedScreenId.isEmpty() || focusedScreenId != screenId) {
                    return;
                }
                const QString assignmentId =
                    m_layoutManager->assignmentIdForScreen(focusedScreenId, curDesktop, currentActivity());
                m_unifiedLayoutController->syncFromExternalState(assignmentId);
            });

    // Connect unified layout controller signals for OSD display
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this,
            [this](PhosphorZones::Layout* layout) {
                if (!layout) {
                    return;
                }
                // Dismiss snap assist — it's stale once the layout changes
                if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
                // Suppress during startup. Mirrors the algorithmChanged gate above.
                // `loadState()` from finalizeStartup() synchronously emits
                // `layoutApplied` per screen as layouts get assigned, and
                // finalizeStartup() is the authoritative startup-OSD path
                // (it calls `showOsdForAllScreens`). Letting this handler also
                // fire double-queues a show on every screen: the first OSD
                // begins its fly-in shader leg, the second show immediately
                // cancels that AV via `cancelTrackingFor` + `parkShaderForReuse`
                // and re-enters via the reuse path. The user sees the slide
                // animation get nuked mid-flight and visually perceives "show
                // doesn't animate", because only the hide leg (no duplicate)
                // plays cleanly.
                if (!m_running) {
                    return;
                }
                // Defer OSD display (same rationale as autotileApplied — first-time
                // QML compilation of the passive shell blocks the event loop
                // ~100-300ms). Capture layout ID (not raw pointer) to avoid
                // use-after-free if the layout is ever replaced between now and
                // next event loop pass.
                if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
                    QUuid layoutId = layout->id();
                    QString screenId = m_unifiedLayoutController->currentScreenName();
                    showLayoutOsdDeferred(layoutId, screenId);
                }
            });

    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::autotileApplied, this,
            [this](const QString& algorithmName, int windowCount) {
                Q_UNUSED(windowCount)
                // Dismiss snap assist — autotile mode replaces snapping entirely
                if (m_overlayService && m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
                // Suppress during startup. Mirrors the algorithmChanged and
                // layoutApplied gates above. `loadState()` from finalizeStartup()
                // synchronously emits `autotileApplied` per screen as layouts get
                // assigned for autotile-mode entries, and finalizeStartup() is the
                // authoritative startup-OSD path (it calls `showOsdForAllScreens`).
                // Without this gate, autotile users would hit the same first-OSD
                // double-queue that the layoutApplied gate fixes for snap-mode
                // users: the first OSD begins its fly-in shader leg, the second
                // show cancels that AV via cancelTrackingFor + parkShaderForReuse,
                // and the slide-in animation gets nuked mid-flight.
                if (!m_running) {
                    return;
                }
                // Defer OSD display so QML window creation (first-time ~100-300ms for
                // NotificationOverlay.qml compilation + scene graph) doesn't block the
                // daemon event loop while the effect is sending windowOpened D-Bus
                // calls. Without this, first toggle to autotile has perceptible lag
                // because the daemon can't process incoming tiling requests until the
                // OSD handler returns.
                if (m_settings && m_settings->showOsdOnLayoutSwitch() && m_autotileEngine && m_overlayService) {
                    QString algorithmId = m_autotileEngine->algorithmId();
                    QString screenId = m_unifiedLayoutController->currentScreenName();
                    showAlgorithmOsdDeferred(algorithmId, algorithmName, screenId);
                }
            });

    // Record manual layout only when user explicitly selects one via zone selector
    // or unified layout controller — NOT on every internal layout change.

    // No handler for OverlayService::manualLayoutSelected: the zone-selector
    // slot is input-transparent by design (ZoneSelectorContent's
    // `interactive: false`) and the QML hover path that used to emit this is
    // gone. Cross-layout commits happen at drop time inside WindowDragAdaptor
    // (drop.cpp), which calls assignLayout + setActiveLayout directly when
    // the user releases the drag on a zone in a different layout. Routing
    // through a hover-driven handler would resnap mid-interaction, which is
    // what produced the "layouts changing when holding alt" bug.
}

void Daemon::connectOverlaySignals()
{
    // No autotileLayoutSelected handler: the zone-selector slot is input-
    // transparent by design (see ZoneSelectorContent's `interactive: false`),
    // so QML never emits a hover-driven selection. Switching the autotile
    // algorithm via the zone-selector popup is gone — users have keyboard
    // shortcuts (NextLayout / QuickLayoutN) and the explicit Layout Picker
    // (Meta+Alt+Space by default) for that.

    // Connect Snap Assist selection: fetch authoritative zone geometry from service (same as
    // keyboard navigation) to avoid overlay coordinate drift/overlap bugs, then forward to effect
    connect(
        m_overlayService.get(), &IOverlayService::snapAssistWindowSelected, this,
        [this](const QString& windowId, const QString& zoneId, const QString& geometryJson, const QString& screenId) {
            // Resolve screen ID; fall back to primary screen
            QString effectiveScreenId = screenId;
            if (effectiveScreenId.isEmpty()) {
                // Prefer effective screen IDs (virtual-screen-aware) over physical screen
                const QStringList effectiveIds =
                    (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());
                if (!effectiveIds.isEmpty()) {
                    effectiveScreenId = effectiveIds.first();
                }
            }
            // Snap assist is a manual-mode concept; ignore if the TARGET screen uses autotile.
            if (isAutotileScreen(effectiveScreenId)) {
                return;
            }
            // If the window is being pulled from a sibling virtual screen that uses
            // autotile, notify the engine so it removes the window from the source
            // screen's tiling tree and retiles the remaining windows.
            if (m_autotileEngine && m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
                const QString sourceScreen = m_windowTrackingAdaptor->service()->screenForWindow(windowId);
                if (!sourceScreen.isEmpty() && sourceScreen != effectiveScreenId && isAutotileScreen(sourceScreen)) {
                    m_autotileEngine->windowClosed(windowId);
                    // Clear autotile-floated marker immediately — windowClosed removes
                    // the window from the engine but doesn't clear the WTS flag.
                    m_autotileEngine->clearModeSpecificFloatMarker(windowId);
                }
            }
            QRect geometry;
            if (!effectiveScreenId.isEmpty() && m_windowTrackingAdaptor) {
                geometry = m_windowTrackingAdaptor->zoneGeometryRect(zoneId, effectiveScreenId);
            }
            if (!geometry.isValid() && !geometryJson.isEmpty()) {
                QJsonObject obj = QJsonDocument::fromJson(geometryJson.toUtf8()).object();
                geometry = QRect(obj[QLatin1String("x")].toInt(), obj[QLatin1String("y")].toInt(),
                                 obj[QLatin1String("width")].toInt(), obj[QLatin1String("height")].toInt());
            }
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->requestMoveSpecificWindowToZone(windowId, zoneId, geometry);
            }
        });

    // Connect navigation feedback signal to show OSD (manual mode: from WindowTrackingAdaptor via KWin effect)
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
            [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
                   const QString& targetZoneId, const QString& screenId) {
                // Suppress resnap OSD when triggered by a mode/layout change
                // (layout switch OSD already provides feedback)
                if (m_suppressResnapOsd > 0
                    && (action == QLatin1String("resnap") || action == QLatin1String("retile"))) {
                    m_suppressResnapOsd = std::max(0, m_suppressResnapOsd - 1);
                    return;
                }
                if (m_settings && m_settings->showNavigationOsd()) {
                    m_overlayService->showNavigationOsd(success, action, reason, sourceZoneId, targetZoneId, screenId);
                }
            });

    // Note: AutotileEngine::navigationFeedbackRequested is relayed through
    // WindowTrackingAdaptor::navigationFeedback via setEngines(), so both engines
    // share the same OSD path through the handler above.
    //
    // KWin effect reports navigation feedback via reportNavigationFeedback D-Bus method,
    // which also emits the Qt navigationFeedback signal.

    // Dismiss snap assist when any window zone assignment changes (navigation, snap, unsnap,
    // float toggle, resnap, etc.). Snap assist is only relevant until the user performs another
    // window operation. During snap assist continuation, the window stays visible (keyboard
    // grab released) so this handler fires and destroys it; showContinuationIfNeeded() then
    // creates a fresh one. For non-continuation selections, this provides the final cleanup.
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowZoneChanged, this,
            [this](const QString& /*windowId*/, const QString& /*zoneId*/) {
                if (m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
            });

    // Mid-drag close cleanup: WindowDragAdaptor holds transient drag state
    // (m_draggedWindowId, m_pendingSnapDragWindowId, m_snapAssistPendingWindowId,
    // overlay refs) that must be torn down if the dragged window closes before
    // endDrag fires. The canonical close path runs through WTA::windowClosed
    // (called by the kwin-effect's notifyWindowClosed); we fan that out
    // in-process here instead of re-exposing a D-Bus surface no external
    // caller was wiring up.
    if (m_windowDragAdaptor) {
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowClosedNotification, m_windowDragAdaptor,
                &WindowDragAdaptor::handleWindowClosed);
    }
}

void Daemon::finalizeStartup()
{
    // Restore autotile state from previous session (window order, algorithm, split ratio)
    // Defers actual retiling until windows are announced by KWin effect
    if (m_autotileEngine) {
        m_autotileEngine->loadState();
    }

    // Now that AutotileEngine::loadState has restored autotile placement records,
    // re-run the exclusion-rule prune so any loaded WindowPlacement records for apps
    // an Exclude rule covers are dropped from the unified store. The init-prologue
    // priming call (daemon.cpp's setExcludeRuleSet/setRules/prune sequence, run
    // synchronously before the rulesChanged subscription wires) already pruned what
    // was loaded then; this re-run covers records that landed during the later
    // autotile load. Patterns derive from the unified Rule store via
    // PhosphorRules::ExclusionRules; the WTA prune removeIf's the placement store.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->pruneExcludedPendingRestores(
            PhosphorRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));
    }

    // Signal that daemon is fully initialized and ready for queries
    Q_EMIT m_layoutAdaptor->daemonReady();

    // Explicitly broadcast settingsChanged so the KWin effect re-fetches all
    // settings (including activation triggers) after daemonReady.  The effect's
    // initial async getSetting() calls can race with daemon construction and
    // return stale/empty values; this second broadcast guarantees a clean load.
    if (m_settingsAdaptor)
        Q_EMIT m_settingsAdaptor->settingsChanged();

    // Show the layout OSD on ALL screens so the user sees what's assigned everywhere.
    // The layoutApplied signal only fires for the focused screen; this covers the rest.
    // Gated on the desktop-switch OSD toggle (not the layout-switch toggle): startup is
    // a passive context change like a desktop switch, not an explicit user-driven layout
    // change, so users who disable desktop-switch OSDs expect quiet startup too.
    //
    // The welcome OSD reads the (screen, desktop, activity) cascade via
    // LayoutRegistry::assignmentIdForScreen. At finalizeStartup time the
    // activity may not yet have arrived from KActivities — currentActivity()
    // returns "", and the cascade misses the user's stored entries (which
    // are keyed on the actual activity UUID). The cascade then falls
    // through to the level-1 default, which may legitimately resolve to
    // autotile (e.g. user has snapping enabled with empty default layout
    // and autotile enabled with a default algorithm) and the OSD shows
    // "Tiling: Binary Split" even though the user is in snapping mode for
    // every configured screen.
    //
    // Defer the show until BOTH (a) the activity is known and (b) we've
    // observed at least one currentDesktopChanged from VirtualDesktop-
    // Manager (or its initial sync read has populated). If activity is
    // already non-empty by the time finalizeStartup runs (KActivities
    // synchronous-init path), fire immediately; otherwise queue on the
    // first activityChanged emission. A short timeout fallback prevents
    // the welcome OSD from being permanently suppressed on systems where
    // KActivities is unavailable (still emits an empty-activity OSD —
    // existing behaviour for activity-less environments).
    if (m_settings && m_settings->showOsdOnDesktopSwitch()) {
        const QString activity = currentActivity();
        // activityReady: either we already have a non-empty activity, or
        // KActivities is unavailable on this system (no point waiting).
        const bool activityReady = !activity.isEmpty() || !PhosphorWorkspaces::ActivityManager::isAvailable();
        if (activityReady) {
            showOsdForAllScreens(activity);
        } else if (m_activityManager) {
            // Defer the welcome OSD until the activity arrives from
            // KActivities, otherwise the cascade walks with empty
            // activity and misses the user's stored entries (which are
            // keyed on the actual activity UUID), silently surfacing
            // the level-1 fallback (e.g. autotile-bsp) on a system
            // configured for snapping. Two arms:
            //
            //   (a) signal — currentActivityChanged fires non-empty;
            //   (b) fallback timer — KActivities never reports.
            //
            // `fired` is the canonical "already shown" flag —
            // QMetaObject::Connection's `operator bool()` reports
            // "the connection was successfully made," NOT "still
            // active," so checking `*conn` after `disconnect(*conn)`
            // does NOT distinguish "still queued" from "already
            // fired and disconnected." A shared_ptr<bool> captured
            // by both arms lets each compare-and-set before doing
            // any work. 5000ms timeout — KActivities autostart on
            // cold boot can take 2-3s on slower systems; 2s
            // observed false-fallback regressions.
            auto conn = std::make_shared<QMetaObject::Connection>();
            auto fired = std::make_shared<bool>(false);
            *conn = connect(m_activityManager.get(), &PhosphorWorkspaces::ActivityManager::currentActivityChanged, this,
                            [this, conn, fired](const QString& a) {
                                if (a.isEmpty() || *fired) {
                                    return;
                                }
                                *fired = true;
                                QObject::disconnect(*conn);
                                showOsdForAllScreens(a);
                            });
            QTimer::singleShot(5000, this, [this, conn, fired]() {
                if (*fired) {
                    return;
                }
                *fired = true;
                QObject::disconnect(*conn);
                // Only fall back to firing the OSD if the activity has now
                // become available. If we land here while currentActivity()
                // is still empty AND KActivities is supposed to be
                // available, firing would replay the very wrong-cascade
                // OSD (autotile-bsp on a snapping-mode user) the deferral
                // was added to avoid — just delayed by 5 s instead of
                // immediate. Suppressing the welcome OSD entirely is the
                // lesser harm: the user gets no welcome banner, but never
                // sees content keyed to the wrong activity.
                const QString activity = currentActivity();
                if (activity.isEmpty() && PhosphorWorkspaces::ActivityManager::isAvailable()) {
                    return;
                }
                showOsdForAllScreens(activity);
            });
        }
    }

    // Prime the active-assignment snapshot now that screens and initial layouts
    // are applied, so the first rule edit diffs against the live assignments
    // (not an empty baseline, which would resnap every screen once).
    diffActiveAssignments();
}

void Daemon::syncAutotileFloatState(const QString& windowId, bool floating, const QString& screenId)
{
    // Sync floating state to PhosphorPlacement::WindowTrackingService and propagate
    // to KWin effect's NavigationHandler::m_floatingWindows via D-Bus signal.
    // Also track autotile origin so mode transitions can distinguish
    // autotile-originated floats from manual snapping-mode floats.
    //
    // Null-guard the engines: this slot is connected to autotile-engine
    // signals, so a queued-connection delivery that lands after
    // m_autotileEngine.reset() (shutdown window) would otherwise deref
    // a freed pointer. Mirrors the windowsReleased connect block above.
    if (!m_autotileEngine || !m_snapEngine) {
        return;
    }
    if (m_windowTrackingAdaptor) {
        PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
        if (floating) {
            m_windowTrackingAdaptor->setWindowFloating(windowId, true);
            m_autotileEngine->markModeSpecificFloated(windowId);
            // Clear stale snap-mode pre-float state ONLY when the pre-float data
            // is for the SAME screen (autotile re-float on the same screen).
            // When the window crosses from a snap VS (e.g. vs:0) to an autotile VS
            // (e.g. vs:1), the pre-float data for vs:0 must be preserved — it's
            // needed to restore the snap zone when the window returns to vs:0.
            const QString preFloatScreen = wts->preFloatScreen(windowId);
            if (preFloatScreen.isEmpty() || preFloatScreen == screenId) {
                wts->clearPreFloatZoneForWindow(windowId);
            }
        } else {
            // The window's snap-mode float (if any) already lives in its placement
            // record's snap slot — captured at the mode-switch snapshot / save time —
            // so there is no parallel saved-float set to update here. The record is
            // the single source of truth and drives the float restore on return to
            // snapping (see windowsReleased).
            m_windowTrackingAdaptor->setWindowFloating(windowId, false);
            m_autotileEngine->clearModeSpecificFloatMarker(windowId);
        }
    }
    // NOTE: Do NOT call unsnapForFloat() here — it destroys the window's
    // zone assignment from manual mode, making it "not snapped" when
    // returning to manual mode. PhosphorZones::Zone assignments are preserved so
    // switching back to manual restores the snapped state.

    // Restore geometry: applyGeometryForFloat prefers pre-snap (the window's
    // original position before any zone snapping) over pre-autotile (autotile tiling).
    // Pre-autotile persists across float/unfloat cycles (the effect's exact-match
    // contains-guard in saveAndRecordPreAutotileGeometry prevents overwriting), so
    // every float restores to the same original position. The effect does NOT apply
    // geometry on float — the daemon is the single source.
    if (floating && m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenId);
    }

    // Use "Floating" and "Tiled" labels for autotile (not "Snapped" for unfloat)
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        QString reason = floating ? QStringLiteral("floated") : QStringLiteral("tiled");
        m_overlayService->showNavigationOsd(true, QStringLiteral("float"), reason, QString(), QString(), screenId);
    }
}

void Daemon::syncAutotileFloatStatePassive(const QString& windowId, bool floating, const QString& screenId)
{
    // Passive state-sync: update WTS to match the engine's internal PhosphorTiles::TilingState,
    // but DO NOT call applyGeometryForFloat. This path is entered when the
    // engine inserts a window whose WTS floating state diverges from the
    // PhosphorTiles::TilingState (e.g. snap→autotile drag drops a window whose WTS still
    // says floating from a prior snap-mode Meta+F). The window already has a
    // valid position (the drop location); teleporting it via the stored
    // pre-tile rect would resize and jump it — discussion #271.
    //
    // Null-guard the engines for the same shutdown-window reason as
    // syncAutotileFloatState above.
    if (!m_autotileEngine || !m_snapEngine || !m_windowTrackingAdaptor) {
        return;
    }
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    // Cross-engine handoff: this signal fires when autotile has just taken
    // ownership of a window that may still be tracked by snap. handoffRelease
    // is a no-op when the engine doesn't track the window, so the
    // isWindowTracked gate below is screen-agnostic (no same-screen-ID
    // comparison) — that closes the loophole where snap thought the window
    // was on the same screen ID as the autotile target (stale state
    // mid-mode-switch) and the previous screen-gated path skipped cleanup.
    // The tracked-check itself only keeps the handoff log meaningful.
    if (m_snapEngine && m_snapEngine->isWindowTracked(windowId)) {
        qCInfo(lcDaemon) << "Cross-engine handoff: releasing snap state for" << windowId << "(autotile screen"
                         << screenId << ")";
        m_snapEngine->handoffRelease(windowId);
    }
    if (floating) {
        m_windowTrackingAdaptor->setWindowFloating(windowId, true);
        m_autotileEngine->markModeSpecificFloated(windowId);
        // Mirror syncAutotileFloatState's cross-VS pre-float preservation so
        // zone-restore on return to the snap VS still works.
        const QString preFloatScreen = wts->preFloatScreen(windowId);
        if (preFloatScreen.isEmpty() || preFloatScreen == screenId) {
            wts->clearPreFloatZoneForWindow(windowId);
        }
    } else {
        // Snap-mode float persists in the placement record's snap slot (single
        // source of truth); nothing to save into a parallel set here.
        m_windowTrackingAdaptor->setWindowFloating(windowId, false);
        m_autotileEngine->clearModeSpecificFloatMarker(windowId);
    }
    // Deliberately no applyGeometryForFloat and no navigation OSD — this is
    // a silent sync of engine↔WTS state, not a user-visible float action.
}

void Daemon::syncAutotileBatchFloatState(const QStringList& windowIds, const QString& screenId)
{
    // Symmetric null-guard with syncAutotileFloatState — shutdown-window
    // queued connections shouldn't deref freed engine pointers.
    if (!m_autotileEngine || !m_windowTrackingAdaptor) {
        return;
    }
    PhosphorPlacement::WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    for (const QString& windowId : windowIds) {
        // Update WTS state directly (not adaptor setWindowFloating: its extra
        // windowStateChanged/placement-capture side emissions are wrong for a
        // batch the effect already applied), but DO route the broadcast
        // bookkeeping through the relay chokepoint. Skipping it left
        // m_broadcastFloating at not-floating for a batch-floated window, so
        // the next genuine unfloat broadcast hit false==false in the gate and
        // was suppressed — stranding the effect's float cache exactly like
        // the silent flips this contract exists to prevent. The relay's
        // windowFloatingChanged emission is redundant for the effect: its
        // float-cache write no-ops (already floating from the
        // windowsTileRequested batch) and the rest of its handler is
        // convergent (coalesced rule reconcile re-resolving to the same
        // state, idempotent snap-tracking clears, a raise of the already-
        // floated window at most). Overflow floats are rare, so the spare
        // signal is the price of keeping the last-broadcast contract
        // single-owner and universal.
        wts->setWindowFloating(windowId, true);
        m_windowTrackingAdaptor->relayWindowFloatingChanged(windowId, true, screenId);
        m_autotileEngine->markModeSpecificFloated(windowId);
        // Same cross-VS preservation logic as the single-window handler
        const QString preFloatScreen = wts->preFloatScreen(windowId);
        if (preFloatScreen.isEmpty() || preFloatScreen == screenId) {
            wts->clearPreFloatZoneForWindow(windowId);
        }
    }
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService && !windowIds.isEmpty()) {
        m_overlayService->showNavigationOsd(true, QStringLiteral("float"), QStringLiteral("overflow"), QString(),
                                            QString(), screenId);
    }
}

} // namespace PlasmaZones
