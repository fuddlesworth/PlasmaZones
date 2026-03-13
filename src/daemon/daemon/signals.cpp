// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "macros.h"
#include "../overlayservice.h"
#include "../modetracker.h"
#include "../unifiedlayoutcontroller.h"
#include "../shortcutmanager.h"
#include "../../core/layoutmanager.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/windowtrackingservice.h"
#include "../../dbus/layoutadaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../dbus/zonedetectionadaptor.h"
#include "../../dbus/windowdragadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../autotile/TilingAlgorithm.h"
#include "../../snap/SnapEngine.h"
#include "../config/settings.h"
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>

namespace PlasmaZones {

void Daemon::initializeAutotile()
{
    // Initialize mode tracker for last-used layout
    m_modeTracker = std::make_unique<ModeTracker>(m_settings.get(), this);
    m_modeTracker->load();

    // Reconcile persisted mode with actual state: if the mode tracker says
    // Autotile but no screen has an autotile assignment (or the feature is
    // disabled), force back to Manual so the popup shows the right layouts.
    if (m_modeTracker->isAutotileMode()) {
        bool hasAutotileAssignment = false;
        if (m_settings->autotileEnabled() && m_layoutManager) {
            for (QScreen* screen : Utils::allScreens()) {
                const QString screenId = Utils::screenIdentifier(screen);
                const int desktop = currentDesktop();
                const QString activity = currentActivity();
                const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
                if (LayoutId::isAutotile(assignmentId)) {
                    hasAutotileAssignment = true;
                    break;
                }
            }
        }
        if (!hasAutotileAssignment) {
            m_modeTracker->setCurrentMode(TilingMode::Manual);
        }
    }

    // Connect autotile engine signals
    if (m_autotileEngine) {
        // Autotile engine signals → OSD (use display name, not algorithm ID)
        // Show OSD when algorithm changes (not on every retile — tilingChanged
        // fires for float, swap, window open/close, etc. which is too noisy)
        connect(m_autotileEngine.get(), &AutotileEngine::algorithmChanged, this, [this](const QString& algorithmId) {
            if (m_modeTracker) {
                m_modeTracker->recordAutotileAlgorithm(algorithmId);
            }
            // Only show OSD when actually in autotile mode — loadState() emits
            // algorithmChanged during startup even if we're in manual mode.
            // Defer OSD display (same rationale as autotileApplied handler above).
            if (m_modeTracker && m_modeTracker->isAutotileMode() && m_settings && m_settings->showOsdOnLayoutSwitch()
                && m_overlayService) {
                auto* algo = AlgorithmRegistry::instance()->algorithm(algorithmId);
                QString displayName = algo ? algo->name() : algorithmId;
                QString screenName =
                    m_unifiedLayoutController ? m_unifiedLayoutController->currentScreenName() : QString();
                if (screenName.isEmpty() && m_windowTrackingAdaptor) {
                    if (QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor)) {
                        screenName = Utils::screenIdentifier(screen);
                    }
                }
                showAlgorithmOsdDeferred(algorithmId, displayName, screenName);
            }
        });

        // Sync autotile float state and show OSD when a window is floated/unfloated
        connect(m_autotileEngine.get(), &AutotileEngine::windowFloatingChanged, this,
                [this](const QString& windowId, bool floating, const QString& screenName) {
                    // Sync floating state to WindowTrackingService and propagate
                    // to KWin effect's NavigationHandler::m_floatingWindows via D-Bus signal.
                    // Also track autotile origin so mode transitions can distinguish
                    // autotile-originated floats from manual snapping-mode floats.
                    if (m_windowTrackingAdaptor) {
                        WindowTrackingService* wts = m_windowTrackingAdaptor->service();
                        if (floating) {
                            m_windowTrackingAdaptor->setWindowFloating(windowId, true);
                            wts->markAutotileFloated(windowId);
                        } else {
                            // If the window was snap-mode-floated (not autotile-floated)
                            // and autotile is clearing it, save for snap-mode restoration.
                            bool wasFloating = wts->isWindowFloating(windowId);
                            bool wasAutotileFloated = wts->isAutotileFloated(windowId);
                            if (wasFloating && !wasAutotileFloated) {
                                wts->saveSnapFloating(windowId);
                                qCInfo(lcDaemon)
                                    << "Saved snap-float for" << windowId << "(autotile clearing stale snap-float)";
                            }
                            m_windowTrackingAdaptor->setWindowFloating(windowId, false);
                            wts->clearAutotileFloated(windowId);
                        }
                    }
                    // NOTE: Do NOT call unsnapForFloat() here — it destroys the window's
                    // zone assignment from manual mode, making it "not snapped" when
                    // returning to manual mode. Zone assignments are preserved so
                    // switching back to manual restores the snapped state.

                    // Restore geometry: applyGeometryForFloat prefers pre-snap (the window's
                    // original position before any zone snapping) over pre-autotile (autotile tiling).
                    // Pre-autotile persists across float/unfloat cycles (the hasSavedGeometryForWindow
                    // guard in the effect prevents overwriting), so every float restores to the same
                    // original position. The effect does NOT apply geometry on float — the daemon is
                    // the single source.
                    if (floating && m_windowTrackingAdaptor) {
                        m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenName);
                    }

                    // Use "Floating" and "Tiled" labels for autotile (not "Snapped" for unfloat)
                    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
                        QString reason = floating ? QStringLiteral("floated") : QStringLiteral("tiled");
                        m_overlayService->showNavigationOsd(true, QStringLiteral("float"), reason, QString(), QString(),
                                                            screenName);
                    }
                });

        // Per-mode float state: when autotile releases windows back to snap mode:
        // 1. Clear autotile-originated floats (overflow + user-float-in-autotile) —
        //    autotile floats don't persist into snap mode. The engine saves them
        //    in m_savedFloatingWindows for restoration on re-entry.
        // 2. Restore snap-mode floats that were saved when entering autotile —
        //    snap floats persist across autotile sessions.
        // Must check isAutotileFloated BEFORE clearing the marker.
        connect(m_autotileEngine.get(), &AutotileEngine::windowsReleasedFromTiling, this,
                [this](const QStringList& windowIds) {
                    if (m_windowTrackingAdaptor) {
                        WindowTrackingService* wts = m_windowTrackingAdaptor->service();
                        for (const QString& windowId : windowIds) {
                            // Clear autotile-originated floats (they don't persist into snap mode)
                            bool wasAutotileFloated = wts->isAutotileFloated(windowId);
                            if (wasAutotileFloated) {
                                m_windowTrackingAdaptor->setWindowFloating(windowId, false);
                            }
                            wts->clearAutotileFloated(windowId);
                            // Restore snap-mode floats that were saved when entering autotile.
                            // Apply pre-tile geometry so the window returns to its floating position.
                            if (wts->restoreSnapFloating(windowId)) {
                                qCInfo(lcDaemon) << "windowsReleasedFromTiling: restoring snap-float for" << windowId;
                                m_windowTrackingAdaptor->setWindowFloating(windowId, true);
                                QString screen = wts->screenAssignments().value(windowId);
                                m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screen);
                            } else {
                                qCDebug(lcDaemon) << "windowsReleasedFromTiling: no snap-float to restore for"
                                                  << windowId << "wasAutotileFloated:" << wasAutotileFloated;
                            }
                        }
                    }
                });

        // ═══════════════════════════════════════════════════════════════════════════
        // Autotile Shortcut Signals
        // ═══════════════════════════════════════════════════════════════════════════

        connect(m_shortcutManager.get(), &ShortcutManager::toggleAutotileRequested, this, [this]() {
            // Feature gate: toggle only works when autotile is enabled in KCM
            if (!m_settings || !m_settings->autotileEnabled()) {
                return;
            }
            if (!m_modeTracker || !m_unifiedLayoutController || !m_layoutManager) {
                return;
            }

            // Resolve focused screen
            QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
            if (!screen) {
                return;
            }
            QString screenId = Utils::screenIdentifier(screen);
            int desktop = currentDesktop();
            QString activity = currentActivity();

            // Set the screen context so applyEntry knows which screen to assign
            m_unifiedLayoutController->setCurrentScreenName(screenId);

            // Temporarily include both layout types so applyLayoutById can find the
            // target across the mode boundary.  The subsequent layoutApplied /
            // autotileApplied signal will call updateLayoutFilter() and restore the
            // correct exclusive filter for the new mode.
            m_unifiedLayoutController->setLayoutFilter(true, true);

            QString currentAssignment = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);

            bool applied = false;
            const bool wasAutotile = LayoutId::isAutotile(currentAssignment);

            // Capture autotile window order BEFORE layout switch destroys TilingState.
            // Merge (not replace) into m_lastAutotileOrders so other desktops' saved
            // orders are preserved — a replace would discard them.
            if (wasAutotile) {
                auto currentOrders = captureAutotileOrders();
                for (auto it = currentOrders.constBegin(); it != currentOrders.constEnd(); ++it) {
                    m_lastAutotileOrders[it.key()] = it.value();
                }
            }

            DesktopContextKey ctxKey{screenId, desktop, activity};

            if (wasAutotile) {
                // Save per-desktop autotile assignment for re-entry
                m_lastAutotileAssignments[ctxKey] = currentAssignment;

                // Autotile → Snapping: restore this desktop's last manual layout.
                // Per-desktop lookup first (set when toggling TO autotile), then
                // fall back to global last manual layout.
                QString layoutId;
                auto manualIt = m_lastManualAssignments.constFind(ctxKey);
                if (manualIt != m_lastManualAssignments.constEnd()) {
                    layoutId = manualIt.value();
                }
                if (layoutId.isEmpty()) {
                    layoutId = m_modeTracker->lastManualLayoutId();
                }
                if (!layoutId.isEmpty()) {
                    applied = m_unifiedLayoutController->applyLayoutById(layoutId);
                }
                // Fallback when lastManualLayoutId is empty (fresh install) or stale
                // (layout was deleted). Use the active layout or first available layout.
                if (!applied) {
                    Layout* fallback = m_layoutManager->activeLayout();
                    if (!fallback && !m_layoutManager->layouts().isEmpty()) {
                        fallback = m_layoutManager->layouts().first();
                    }
                    if (fallback) {
                        applied = m_unifiedLayoutController->applyLayoutById(fallback->id().toString());
                    }
                }
            } else {
                // Save per-desktop manual assignment for re-exit
                m_lastManualAssignments[ctxKey] = currentAssignment;

                // Pre-save snap-float state before autotile entry so rapid
                // toggles don't lose snap-mode floats (see presaveSnapFloats doc).
                presaveSnapFloats();

                // Pre-seed autotile engine with saved autotile order (if available)
                // or zone-ordered windows. Seed ALL screens for deterministic ordering
                // on multi-monitor setups.
                for (QScreen* s : m_screenManager->screens()) {
                    seedAutotileOrderForScreen(s->name());
                }

                // Resolve algorithm: prefer this desktop's last-used algorithm,
                // then fall back to the user's configured default (settings).
                // Do NOT use resolveAlgorithmId() here — it checks the global
                // ModeTracker lastAutotileAlgorithm first, which is whatever
                // algorithm was last used on ANY desktop. The default autotile
                // setting should behave like the default snapping layout: each
                // desktop without an explicit per-desktop assignment gets the
                // configured default, not the global last-used.
                QString algoId;
                auto savedIt = m_lastAutotileAssignments.constFind(ctxKey);
                if (savedIt != m_lastAutotileAssignments.constEnd()) {
                    algoId = LayoutId::extractAlgorithmId(savedIt.value());
                }
                if (algoId.isEmpty() && m_settings) {
                    algoId = m_settings->autotileAlgorithm();
                }
                if (algoId.isEmpty()) {
                    algoId = AlgorithmRegistry::defaultAlgorithmId();
                }
                if (!algoId.isEmpty()) {
                    applied = m_unifiedLayoutController->applyLayoutById(LayoutId::makeAutotileId(algoId));
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
            if (applied && wasAutotile && m_snapEngine) {
                // Build exclusion set: windows that fit into the target layout's zones
                // will be zone-snapped by the resnap D-Bus signal. Without excluding them,
                // restoreAutotileOnlyGeometries sends float-geometry D-Bus calls that
                // arrive AFTER the resnap and overwrite the zone positions.
                // Use per-screen zone count (not global activeLayout) because each screen
                // may have a different layout assigned with a different zone count.
                // Only process entries for the CURRENT desktop (m_lastAutotileOrders
                // accumulates entries across desktops after the merge fix).
                WindowTrackingService* wts = m_windowTrackingAdaptor ? m_windowTrackingAdaptor->service() : nullptr;
                int resnapScreenCount = 0;
                QSet<QString> resnappedWindows;
                for (auto it = m_lastAutotileOrders.constBegin(); it != m_lastAutotileOrders.constEnd(); ++it) {
                    if (it.key().desktop != desktop || it.key().activity != activity) {
                        continue;
                    }
                    ++resnapScreenCount;
                    const QStringList& fullOrder = it.value();
                    const QString& screenName = it.key().screenId;

                    // Filter out floating windows — windowsReleasedFromTiling already
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

                    Layout* screenLayout = m_layoutManager->resolveLayoutForScreen(screenName);
                    int zoneCount = screenLayout ? screenLayout->zoneCount() : 0;
                    for (int i = 0; i < std::min(static_cast<int>(windowOrder.size()), zoneCount); ++i) {
                        resnappedWindows.insert(windowOrder.at(i));
                    }
                    m_snapEngine->resnapFromAutotileOrder(windowOrder, screenName);
                }
                m_suppressResnapOsd = resnapScreenCount;

                restoreAutotileOnlyGeometries(resnappedWindows);
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
    // Initialize unified layout controller (manual layouts only)
    m_unifiedLayoutController = std::make_unique<UnifiedLayoutController>(m_layoutManager.get(), m_settings.get(),
                                                                          m_autotileEngine.get(), this);

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

    // Pre-warm Layout OSD QML windows unconditionally.
    // First-time QML compilation of LayoutOsd.qml (~100-300ms) would otherwise
    // block the event loop during the first layout switch (manual or autotile),
    // causing perceptible lag.  Deferred so daemon init completes first.
    if (m_overlayService) {
        QTimer::singleShot(0, this, [this]() {
            m_overlayService->warmUpLayoutOsd();
        });
    }

    // Update layout filter when tiling mode changes at runtime
    connect(m_modeTracker.get(), &ModeTracker::currentModeChanged, this, &Daemon::updateLayoutFilter);

    // Note: autotileEnabledChanged, snappingEnabledChanged, and perScreenAutotileSettingsChanged
    // are handled by the settingsChanged handler above (consolidated for single-pass processing).
    // Individual autotile signals only fire from runtime setters, where settingsChanged also
    // fires and handles the transitions — no separate handlers needed.

    // Re-derive autotile screens when assignments change (e.g., from D-Bus, layout picker)
    connect(m_layoutManager.get(), &LayoutManager::layoutAssigned, this, [this]() {
        updateAutotileScreens();
    });

    // Connect unified layout controller signals for OSD display and mode tracking
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this, [this](Layout* layout) {
        if (m_modeTracker) {
            m_modeTracker->setCurrentMode(TilingMode::Manual);
            m_modeTracker->recordManualLayout(layout->id());
        }
        // Defer OSD display (same rationale as autotileApplied — first-time QML
        // compilation of LayoutOsd.qml blocks the event loop ~100-300ms).
        // Capture layout ID (not raw pointer) to avoid use-after-free if the
        // layout is ever replaced between now and next event loop pass.
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            QUuid layoutId = layout->id();
            QString screenName = m_unifiedLayoutController->currentScreenName();
            showLayoutOsdDeferred(layoutId, screenName);
        }
    });

    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::autotileApplied, this,
            [this](const QString& algorithmName, int windowCount) {
                Q_UNUSED(windowCount)
                if (m_modeTracker) {
                    m_modeTracker->setCurrentMode(TilingMode::Autotile);
                }
                // Defer OSD display so QML window creation (first-time ~100-300ms for
                // LayoutOsd.qml compilation + scene graph) doesn't block the daemon event
                // loop while the effect is sending windowOpened D-Bus calls.  Without this,
                // first toggle to autotile has perceptible lag because the daemon can't
                // process incoming tiling requests until the OSD handler returns.
                if (m_settings && m_settings->showOsdOnLayoutSwitch() && m_autotileEngine && m_overlayService) {
                    QString algorithmId = m_autotileEngine->algorithm();
                    QString screenName = m_unifiedLayoutController->currentScreenName();
                    showAlgorithmOsdDeferred(algorithmId, algorithmName, screenName);
                }
            });

    // Record manual layout only when user explicitly selects one via zone selector
    // or unified layout controller — NOT on every internal layout change.

    // Connect zone selector manual layout selection (drop on zone)
    // Screen name comes directly from the zone selector window
    connect(m_overlayService.get(), &OverlayService::manualLayoutSelected, this,
            [this](const QString& layoutId, const QString& screenName) {
                if (!m_layoutManager) {
                    return;
                }
                Layout* layout = m_layoutManager->layoutById(QUuid::fromString(layoutId));
                if (!layout) {
                    return;
                }
                if (!screenName.isEmpty()) {
                    QString screenId = Utils::screenIdForName(screenName);
                    m_layoutManager->assignLayout(screenId, m_virtualDesktopManager->currentDesktop(),
                                                  m_activityManager && ActivityManager::isAvailable()
                                                      ? m_activityManager->currentActivity()
                                                      : QString(),
                                                  layout);
                }
                // Always update global active layout — fires activeLayoutChanged which
                // populates the resnap buffer, cleans stale assignments, updates OSD, etc.
                m_layoutManager->setActiveLayout(layout);
                qCInfo(lcDaemon) << "Zone selector: manual layout selected, layout=" << layout->name()
                                 << "screen=" << screenName;
                m_overlayService->showLayoutOsd(layout, screenName);
                if (m_modeTracker) {
                    m_modeTracker->recordManualLayout(layout->id());
                }
            });
}

void Daemon::connectOverlaySignals()
{
    // Connect zone selector autotile layout selection — route through UnifiedLayoutController
    // to avoid duplicate activation logic (the controller handles enable + algorithm + OSD)
    connect(m_overlayService.get(), &IOverlayService::autotileLayoutSelected, this,
            [this](const QString& algorithmId, const QString& screenName) {
                Q_UNUSED(screenName)
                if (m_unifiedLayoutController) {
                    m_unifiedLayoutController->applyLayoutById(LayoutId::makeAutotileId(algorithmId));
                }
            });

    // Connect Snap Assist selection: fetch authoritative zone geometry from service (same as
    // keyboard navigation) to avoid overlay coordinate drift/overlap bugs, then forward to effect
    connect(
        m_overlayService.get(), &IOverlayService::snapAssistWindowSelected, this,
        [this](const QString& windowId, const QString& zoneId, const QString& geometryJson, const QString& screenName) {
            // Resolve screen name first (needed for per-screen autotile check)
            QString geometryToUse = geometryJson;
            QString effectiveScreen = screenName;
            if (effectiveScreen.isEmpty() && QGuiApplication::primaryScreen()) {
                effectiveScreen = QGuiApplication::primaryScreen()->name();
            }
            // Snap assist is a manual-mode concept; ignore if this screen uses autotile
            if (m_autotileEngine && m_autotileEngine->isAutotileScreen(effectiveScreen)) {
                return;
            }
            if (!effectiveScreen.isEmpty()) {
                QString authGeometry = m_windowTrackingAdaptor->getZoneGeometryForScreen(zoneId, effectiveScreen);
                if (!authGeometry.isEmpty()) {
                    geometryToUse = authGeometry;
                }
            }
            m_windowTrackingAdaptor->requestMoveSpecificWindowToZone(windowId, zoneId, geometryToUse);
        });

    // Connect navigation feedback signal to show OSD (manual mode: from WindowTrackingAdaptor via KWin effect)
    connect(
        m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
        [this](bool success, const QString& action, const QString& reason, const QString& sourceZoneId,
               const QString& targetZoneId, const QString& screenName) {
            // Suppress resnap OSD when triggered by a mode/layout change
            // (layout switch OSD already provides feedback)
            if (m_suppressResnapOsd > 0 && (action == QStringLiteral("resnap") || action == QStringLiteral("retile"))) {
                m_suppressResnapOsd = std::max(0, m_suppressResnapOsd - 1);
                return;
            }
            if (m_settings && m_settings->showNavigationOsd()) {
                m_overlayService->showNavigationOsd(success, action, reason, sourceZoneId, targetZoneId, screenName);
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
    // window operation. The snap assist's own selection path already calls root.close() in QML,
    // so this is a no-op for that case (isSnapAssistVisible returns false).
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowZoneChanged, this,
            [this](const QString& /*windowId*/, const QString& /*zoneId*/) {
                if (m_overlayService->isSnapAssistVisible()) {
                    m_overlayService->hideSnapAssist();
                }
            });

    // Connect to KWin script
    connectToKWinScript();
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
}

} // namespace PlasmaZones
