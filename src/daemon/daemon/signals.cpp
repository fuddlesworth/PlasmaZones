// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../modetracker.h"
#include "../unifiedlayoutcontroller.h"
#include "../shortcutmanager.h"
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorScreens/Manager.h>
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/logging.h"
#include "../../core/constants.h"
#include "../../core/utils.h"
#include "../../core/windowtrackingservice.h"
#include "../../dbus/layoutadaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../dbus/zonedetectionadaptor.h"

#include <QJsonDocument>
#include <QJsonObject>
#include "../../dbus/windowdragadaptor.h"
#include <PhosphorEngineApi/PlacementEngineBase.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include "../../snap/SnapEngine.h"
#include "../config/settings.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

using PlacementEngineBase = PhosphorEngineApi::PlacementEngineBase;

namespace PlasmaZones {

void Daemon::initializeAutotile()
{
    // Initialize mode tracker (thin delegate to LayoutManager's per-context PhosphorZones::AssignmentEntry)
    m_modeTracker = std::make_unique<ModeTracker>(m_settings.get(), m_layoutManager.get(), m_screenManager.get(), this);

    // Connect autotile engine signals
    if (m_autotileEngine) {
        // Autotile engine signals → OSD (use display name, not algorithm ID)
        // Show OSD when algorithm changes (not on every retile — tilingChanged
        // fires for float, swap, window open/close, etc. which is too noisy)
        connect(m_autotileEngine.get(), &PlacementEngineBase::algorithmChanged, this,
                [this](const QString& algorithmId) {
                    // Only show OSD when actually in autotile mode — loadState() emits
                    // algorithmChanged during startup even if we're in manual mode.
                    // Defer OSD display (same rationale as autotileApplied handler above).
                    if (m_modeTracker && m_modeTracker->isAnyScreenAutotile() && m_settings
                        && m_settings->showOsdOnLayoutSwitch() && m_overlayService) {
                        auto* algo = m_algorithmRegistry.get()->algorithm(algorithmId);
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
        connect(m_autotileEngine.get(), &PhosphorEngineApi::PlacementEngineBase::windowFloatingChanged, this,
                &Daemon::syncAutotileFloatState);

        // Passive float state sync for engine-internal state divergence (e.g. a
        // newly-inserted window carrying stale snap-mode float state). Routes to
        // a handler that updates WTS bookkeeping without calling
        // applyGeometryForFloat — the window already has a valid position and
        // must not be teleported to a stored pre-tile rect.
        connect(m_autotileEngine.get(), &PhosphorEngineApi::PlacementEngineBase::windowFloatingStateSynced, this,
                &Daemon::syncAutotileFloatStatePassive);

        // Batch overflow float handler: overflow windows are included in the
        // windowsTileRequested D-Bus signal with "floating" flag, so the effect
        // handles geometry restore directly. Here we only update daemon-side
        // WTS state without emitting per-window D-Bus signals.
        connect(m_autotileEngine.get(), &PhosphorEngineApi::PlacementEngineBase::windowsBatchFloated, this,
                &Daemon::syncAutotileBatchFloatState);

        // Per-mode float state: when autotile releases windows back to snap mode:
        // 1. Clear autotile-originated floats (overflow + user-float-in-autotile) —
        //    autotile floats don't persist into snap mode. The engine saves them
        //    in m_savedFloatingWindows for restoration on re-entry.
        // 2. Restore snap-mode floats that were saved when entering autotile —
        //    snap floats persist across autotile sessions.
        // Must check isModeSpecificFloated BEFORE clearing the marker.
        connect(m_autotileEngine.get(), &PlacementEngineBase::windowsReleased, this,
                [this](const QStringList& windowIds, const QSet<QString>& releasedScreenIds) {
                    if (m_windowTrackingAdaptor) {
                        WindowTrackingService* wts = m_windowTrackingAdaptor->service();
                        m_pendingSnapFloatRestores.clear();
                        for (const QString& windowId : windowIds) {
                            // Only process windows whose current WTS screen is one of the
                            // screens being released. A window that moved to a different
                            // screen (e.g., dragged from autotile VS to snap VS and resnapped)
                            // is no longer on the releasing screen — its state on the current
                            // screen must not be disturbed.
                            const QString windowScreen = wts->screenAssignments().value(windowId);
                            if (!windowScreen.isEmpty() && !releasedScreenIds.contains(windowScreen)) {
                                // Window is on a different screen — do NOT touch its state.
                                // It may be on another autotile screen (flag still valid) or
                                // a snap screen (flag already cleared by assignWindowToZones).
                                qCDebug(lcDaemon) << "windowsReleased: skipping" << windowId << "on screen"
                                                  << windowScreen << "(not in released set)";
                                continue;
                            }
                            // Clear autotile-originated floats (they don't persist into snap mode)
                            bool wasAutotileFloated = m_autotileEngine->isModeSpecificFloated(windowId);
                            if (wasAutotileFloated) {
                                m_windowTrackingAdaptor->setWindowFloating(windowId, false);
                            }
                            m_autotileEngine->clearModeSpecificFloatMarker(windowId);
                            // Restore snap-mode floats that were saved when entering autotile.
                            // Float state is set immediately (lightweight cache update), but
                            // geometry restore is deferred to the batched resnap signal to
                            // avoid individual D-Bus signals queuing behind the resnap.
                            if (m_snapEngine->restoreSavedModeFloat(windowId)) {
                                qCInfo(lcDaemon) << "windowsReleased: restoring snap-float for" << windowId;
                                m_windowTrackingAdaptor->setWindowFloating(windowId, true);
                                QString screen = wts->screenAssignments().value(windowId);
                                auto geo = wts->validatedUnmanagedGeometry(windowId, screen);
                                if (geo) {
                                    ZoneAssignmentEntry entry;
                                    entry.windowId = windowId;
                                    entry.targetZoneId = RestoreSentinel;
                                    entry.targetGeometry = *geo;
                                    m_pendingSnapFloatRestores.append(entry);
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

            // Resolve focused screen
            const QString screenId = resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor);
            if (screenId.isEmpty()) {
                qCWarning(lcDaemon) << "Mode toggle: empty screenId from resolveShortcutScreenId";
                return;
            }
            int desktop = currentDesktop();
            QString activity = currentActivity();
            qCInfo(lcDaemon) << "Mode toggle: screenId=" << screenId << "desktop=" << desktop
                             << "activity=" << activity;

            // Context gate: if PlasmaZones is disabled for this screen/desktop/activity,
            // show a visual OSD explaining why instead of silently ignoring the toggle.
            // Note: intentionally shown regardless of showOsdOnLayoutSwitch — this is
            // direct feedback to an explicit user action, not a passive layout-switch OSD.
            const DisabledReason why = contextDisabledReason(m_settings.get(), screenId, desktop, activity);
            if (why != DisabledReason::NotDisabled) {
                showContextDisabledOsd(screenId, desktop, activity, why);
                return;
            }

            // Set context so ModeTracker reads from the correct per-desktop entry
            if (m_modeTracker) {
                m_modeTracker->setContext(screenId, desktop, activity);
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
                // scopes, then to the user's configured default (settings).
                QString algoId = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity);
                if (algoId.isEmpty() && !activity.isEmpty()) {
                    algoId = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, QString());
                }
                if (algoId.isEmpty() && m_settings) {
                    algoId = m_settings->defaultAutotileAlgorithm();
                }
                if (algoId.isEmpty()) {
                    algoId = PhosphorTiles::AlgorithmRegistry::staticDefaultAlgorithmId();
                }
                if (!algoId.isEmpty()) {
                    applied =
                        m_unifiedLayoutController->applyLayoutById(PhosphorLayout::LayoutId::makeAutotileId(algoId));
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
            // pre-autotile floating geometry restored by restoreAutotileOnlyGeometries.
            auto* concreteSnap = qobject_cast<SnapEngine*>(m_snapEngine.get());
            if (applied && wasAutotile && !concreteSnap && m_snapEngine) {
                qCWarning(lcDaemon) << "Snap engine is not a SnapEngine — autotile→snap resnap skipped";
            }
            if (applied && wasAutotile && concreteSnap) {
                // Build exclusion set: windows that fit into the target layout's zones
                // will be zone-snapped by the resnap D-Bus signal. Without excluding them,
                // restoreAutotileOnlyGeometries sends float-geometry D-Bus calls that
                // arrive AFTER the resnap and overwrite the zone positions.
                // Use per-screen zone count (not global activeLayout) because each screen
                // may have a different layout assigned with a different zone count.
                // Only process entries for the CURRENT desktop (m_lastAutotileOrders
                // accumulates entries across desktops after the merge fix).
                //
                // Batch all resnap entries into ONE signal to eliminate the race condition
                // where multiple per-screen signals cause the effect to restore geometries
                // and resnap in parallel.
                WindowTrackingService* wts = m_windowTrackingAdaptor ? m_windowTrackingAdaptor->service() : nullptr;
                QSet<QString> resnappedWindows;
                QVector<ZoneAssignmentEntry> allResnapEntries;
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
                    // Also skip windows with no zone assignment (never snapped before
                    // autotile) — they get pre-autotile geometry via restoreAutotileOnlyGeometries.
                    QStringList windowOrder;
                    for (const QString& windowId : fullOrder) {
                        if (wts && wts->isWindowFloating(windowId)) {
                            continue;
                        }
                        if (wts && wts->zoneAssignments().value(windowId).isEmpty()) {
                            continue;
                        }
                        windowOrder.append(windowId);
                    }

                    PhosphorZones::Layout* screenLayout = m_layoutManager->resolveLayoutForScreen(resnapScreenId);
                    int zoneCount = screenLayout ? screenLayout->zoneCount() : 0;
                    for (int i = 0; i < std::min(static_cast<int>(windowOrder.size()), zoneCount); ++i) {
                        resnappedWindows.insert(windowOrder.at(i));
                    }
                    QVector<ZoneAssignmentEntry> entries =
                        concreteSnap->calculateResnapEntriesFromAutotileOrder(windowOrder, resnapScreenId);
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
                m_suppressResnapOsd = allResnapEntries.isEmpty() ? 0 : 1;
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
    m_layoutManager->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());
    m_unifiedLayoutController->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());
    if (m_activityManager && ActivityManager::isAvailable()) {
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

    // Pre-warm PhosphorZones::Layout OSD QML windows unconditionally.
    // First-time QML compilation of LayoutOsd.qml (~100-300ms) would otherwise
    // block the event loop during the first layout switch (manual or autotile),
    // causing perceptible lag.  Deferred so daemon init completes first.
    if (m_overlayService) {
        QTimer::singleShot(0, this, [this]() {
            m_overlayService->warmUpLayoutOsd();
            m_overlayService->warmUpNavigationOsd();
        });
    }

    // Note: autotileEnabledChanged, snappingEnabledChanged, and perScreenAutotileSettingsChanged
    // are handled by the settingsChanged handler above (consolidated for single-pass processing).
    // Individual autotile signals only fire from runtime setters, where settingsChanged also
    // fires and handles the transitions — no separate handlers needed.

    // Re-derive autotile screens and layout filter when assignments change
    // (e.g., from D-Bus, layout picker, unified controller).
    // Also sync the unified controller's cycling index when the assignment
    // affects the current desktop — needed for D-Bus/KCM batch operations.
    // Do NOT touch setActiveLayout here — applyEntry/manualLayoutSelected
    // handle active layout themselves, and calling it here with QSignalBlocker
    // steals the activeLayoutChanged transition, leaving the resnap buffer
    // empty. Desktop switches sync active layout via syncModeFromAssignments().
    connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutAssigned, this,
            [this](const QString& screenId, int virtualDesktop, PhosphorZones::Layout* /*layout*/) {
                updateAutotileScreens();
                updateLayoutFilter();

                // Sync unified controller cycling index when assignment affects current desktop.
                const int curDesktop = currentDesktop();
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
                // Dismiss snap assist — it's stale once the layout changes
                if (m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
                // Defer OSD display (same rationale as autotileApplied — first-time QML
                // compilation of LayoutOsd.qml blocks the event loop ~100-300ms).
                // Capture layout ID (not raw pointer) to avoid use-after-free if the
                // layout is ever replaced between now and next event loop pass.
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
                if (m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
                // Defer OSD display so QML window creation (first-time ~100-300ms for
                // LayoutOsd.qml compilation + scene graph) doesn't block the daemon event
                // loop while the effect is sending windowOpened D-Bus calls.  Without this,
                // first toggle to autotile has perceptible lag because the daemon can't
                // process incoming tiling requests until the OSD handler returns.
                if (m_settings && m_settings->showOsdOnLayoutSwitch() && m_autotileEngine && m_overlayService) {
                    QString algorithmId = m_autotileEngine->algorithmId();
                    QString screenId = m_unifiedLayoutController->currentScreenName();
                    showAlgorithmOsdDeferred(algorithmId, algorithmName, screenId);
                }
            });

    // Record manual layout only when user explicitly selects one via zone selector
    // or unified layout controller — NOT on every internal layout change.

    // Connect zone selector manual layout selection (drop on zone)
    // Screen name comes directly from the zone selector window.
    // Routes through UnifiedLayoutController::applyLayoutById() + resnapIfManualMode(),
    // the same path as cycle/quick-layout shortcuts, for consistent resnap behavior.
    connect(m_overlayService.get(), &OverlayService::manualLayoutSelected, this,
            [this](const QString& layoutId, const QString& screenId) {
                if (!m_layoutManager || !m_unifiedLayoutController) {
                    return;
                }
                // Check if snapping layout is locked
                // screenId is already a virtual-aware ID from the zone selector
                if (!screenId.isEmpty() && isCurrentContextLockedForMode(screenId, 0)) {
                    showLockedPreviewOsd(screenId);
                    return;
                }
                if (!screenId.isEmpty()) {
                    m_unifiedLayoutController->setCurrentScreenName(screenId);
                }
                if (!m_unifiedLayoutController->applyLayoutById(layoutId)) {
                    return;
                }
                qCInfo(lcDaemon) << "Zone selector: manual layout selected, layout=" << layoutId
                                 << "screen=" << screenId;
                resnapIfManualMode();
            });
}

void Daemon::connectOverlaySignals()
{
    // Connect zone selector autotile layout selection — route through UnifiedLayoutController
    // to avoid duplicate activation logic (the controller handles enable + algorithm + OSD)
    connect(m_overlayService.get(), &IOverlayService::autotileLayoutSelected, this,
            [this](const QString& algorithmId, const QString& screenId) {
                // Check if tiling algorithm is locked
                // screenId is already a virtual-aware ID from the zone selector
                if (!screenId.isEmpty() && isCurrentContextLockedForMode(screenId, 1)) {
                    showLockedPreviewOsd(screenId);
                    return;
                }
                if (m_unifiedLayoutController) {
                    if (!screenId.isEmpty()) {
                        m_unifiedLayoutController->setCurrentScreenName(screenId);
                    }
                    m_unifiedLayoutController->applyLayoutById(PhosphorLayout::LayoutId::makeAutotileId(algorithmId));
                }
            });

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
                const QString sourceScreen = m_windowTrackingAdaptor->service()->screenAssignments().value(windowId);
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
                    && (action == QStringLiteral("resnap") || action == QStringLiteral("retile"))) {
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
}

void Daemon::finalizeStartup()
{
    // Restore autotile state from previous session (window order, algorithm, split ratio)
    // Defers actual retiling until windows are announced by KWin effect
    if (m_autotileEngine) {
        m_autotileEngine->loadState();
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
    if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
        showOsdForAllScreens(currentDesktop(), currentActivity());
    }
}

void Daemon::syncAutotileFloatState(const QString& windowId, bool floating, const QString& screenId)
{
    // Sync floating state to WindowTrackingService and propagate
    // to KWin effect's NavigationHandler::m_floatingWindows via D-Bus signal.
    // Also track autotile origin so mode transitions can distinguish
    // autotile-originated floats from manual snapping-mode floats.
    if (m_windowTrackingAdaptor) {
        WindowTrackingService* wts = m_windowTrackingAdaptor->service();
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
            // If the window was snap-mode-floated (not autotile-floated)
            // and autotile is clearing it, save for snap-mode restoration.
            bool wasFloating = wts->isWindowFloating(windowId);
            bool wasAutotileFloated = m_autotileEngine->isModeSpecificFloated(windowId);
            if (wasFloating && !wasAutotileFloated) {
                m_snapEngine->saveModeFloat(windowId);
                qCInfo(lcDaemon) << "Saved snap-float for" << windowId << "(autotile clearing stale snap-float)";
            }
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
    // Pre-autotile persists across float/unfloat cycles (the hasSavedGeometryForWindow
    // guard in the effect prevents overwriting), so every float restores to the same
    // original position. The effect does NOT apply geometry on float — the daemon is
    // the single source.
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
    if (!m_windowTrackingAdaptor) {
        return;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
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
        bool wasFloating = wts->isWindowFloating(windowId);
        bool wasAutotileFloated = m_autotileEngine->isModeSpecificFloated(windowId);
        if (wasFloating && !wasAutotileFloated) {
            m_snapEngine->saveModeFloat(windowId);
            qCInfo(lcDaemon) << "Saved snap-float for" << windowId << "(passive sync clearing stale snap-float)";
        }
        m_windowTrackingAdaptor->setWindowFloating(windowId, false);
        m_autotileEngine->clearModeSpecificFloatMarker(windowId);
    }
    // Deliberately no applyGeometryForFloat and no navigation OSD — this is
    // a silent sync of engine↔WTS state, not a user-visible float action.
}

void Daemon::syncAutotileBatchFloatState(const QStringList& windowIds, const QString& screenId)
{
    if (!m_windowTrackingAdaptor) {
        return;
    }
    WindowTrackingService* wts = m_windowTrackingAdaptor->service();
    for (const QString& windowId : windowIds) {
        // Update WTS state directly — don't call setWindowFloating()
        // on the adaptor since that emits a D-Bus windowFloatingChanged
        // signal which the effect doesn't need (it already processed
        // the float from the windowsTileRequested batch).
        wts->setWindowFloating(windowId, true);
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
