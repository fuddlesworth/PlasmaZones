// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../shortcutmanager.h"
#include "../../core/layoutmanager.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/geometryutils.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../dbus/layoutadaptor.h"
#include "../../dbus/settingsadaptor.h"
#include "../../dbus/shaderadaptor.h"
#include "../../dbus/compositorbridgeadaptor.h"
#include "../../dbus/controladaptor.h"
#include "../../dbus/overlayadaptor.h"
#include "../../dbus/zonedetectionadaptor.h"
#include "../../dbus/windowtrackingadaptor.h"
#include "../../dbus/screenadaptor.h"
#include "../../dbus/windowdragadaptor.h"
#include "../../dbus/autotileadaptor.h"
#include "../../dbus/snapadaptor.h"
#include "../../autotile/AutotileEngine.h"
#include "../../autotile/AlgorithmRegistry.h"
#include "../../autotile/TilingAlgorithm.h"
#include "../../autotile/algorithms/ScriptedAlgorithmLoader.h"
#include "../../snap/SnapEngine.h"
#include "../../core/shaderregistry.h"
#include "../../core/zonedetector.h"
#include <QProcess>
#include <QPointer>
#include "../config/settings.h"
#include <QGuiApplication>
#include <QScreen>

namespace PlasmaZones {

void Daemon::connectScreenSignals()
{
    // Initialize and start screen manager
    m_screenManager->init();
    m_screenManager->start();

    // Warn about identical monitors producing duplicate screen IDs
    Utils::warnDuplicateScreenIds();

    // Load saved virtual screen configs from Settings into ScreenManager
    const auto vsConfigs = m_settings->virtualScreenConfigs();
    for (auto it = vsConfigs.constBegin(); it != vsConfigs.constEnd(); ++it) {
        m_screenManager->setVirtualScreenConfig(it.key(), it.value());
    }

    // Persist virtual screen config changes back to Settings, and migrate screen assignments
    connect(m_screenManager.get(), &ScreenManager::virtualScreensChanged, this, [this](const QString& physId) {
        auto config = m_screenManager->virtualScreenConfig(physId);
        m_settings->setVirtualScreenConfig(physId, config);
        // Clear stale resnap buffer — screen IDs have changed
        if (m_windowTrackingAdaptor) {
            m_windowTrackingAdaptor->service()->clearResnapBuffer();
        }
        // Migrate window screen assignments when virtual screens change at runtime
        if (config.hasSubdivisions() && m_windowTrackingAdaptor) {
            QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
            m_windowTrackingAdaptor->service()->migrateScreenAssignmentsToVirtual(physId, vsIds, m_screenManager.get());
        }
        // Prune stale autotile order entries for old virtual screen IDs
        pruneAutotileOrdersForRemovedScreens(physId);
        // Trigger geometry recalculation and window resnap so snapped windows
        // reposition to the new virtual screen bounds (e.g. 50/50 → 60/40).
        // Reuse the same debounced path as physical screen geometry changes.
        QScreen* physScreen = ScreenManager::resolvePhysicalScreen(physId);
        if (physScreen) {
            m_geometryUpdatePending = true;
            m_geometryUpdateTimer.start();
        }
    });

    // Connect screen manager signals
    connect(m_screenManager.get(), &ScreenManager::screenAdded, this, [this](QScreen* screen) {
        // Invalidate cached EDID serial so a fresh sysfs read happens for this connector
        // (handles the case where EDID wasn't available during very early startup)
        Utils::invalidateEdidCache(screen->name());
        m_overlayService->handleScreenAdded(screen);
        // Recalculate zone geometries for all effective screen IDs on this physical screen
        const QString physId = Utils::screenIdentifier(screen);
        const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
        const int desktop = m_virtualDesktopManager->currentDesktop();
        const QString activity =
            m_activityManager && ActivityManager::isAvailable() ? m_activityManager->currentActivity() : QString();
        for (const QString& sid : vsIds) {
            Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
            if (screenLayout) {
                screenLayout->recalculateZoneGeometries(GeometryUtils::effectiveScreenGeometry(screenLayout, sid));
            }
        }
    });

    connect(m_screenManager.get(), &ScreenManager::screenRemoved, this, [this](QScreen* screen) {
        m_overlayService->handleScreenRemoved(screen);

        // Capture screen ID BEFORE invalidating cache (screenIdentifier reads cached EDID)
        const QString removedName = screen->name();
        const QString removedScreenId = Utils::screenIdentifier(screen);

        // Invalidate cached EDID serial so a different monitor on this connector is detected
        Utils::invalidateEdidCache(removedName);

        // Clean stale entries from layout visibility restrictions
        // Check both screen ID (new) and connector name (legacy)
        for (Layout* layout : m_layoutManager->layouts()) {
            QStringList allowed = layout->allowedScreens();
            if (allowed.isEmpty())
                continue;
            bool changed = false;
            changed |= (allowed.removeAll(removedScreenId) > 0);
            changed |= (allowed.removeAll(removedName) > 0);
            if (changed) {
                layout->setAllowedScreens(allowed);
            }
        }
    });

    connect(m_screenManager.get(), &ScreenManager::screenGeometryChanged, this, [this] {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    });

    // Connect to available geometry changes (panels added/removed/resized)
    // This is reactive - the sensor windows automatically track panel changes
    // Uses debouncing to coalesce rapid changes into a single update
    connect(m_screenManager.get(), &ScreenManager::availableGeometryChanged, this, [this] {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    });

    // Don't pre-create overlay windows at startup. On Wayland with the layer-shell
    // QPA plugin this can cause visibility issues. Create on-demand in show() instead,
    // which also avoids the overlay flashing during login.
    qCInfo(lcDaemon) << "Overlay service: ready," << m_screenManager->screens().count()
                     << "screens available (windows created on-demand)";
}

void Daemon::connectDesktopActivity()
{
    // Initialize and start virtual desktop manager
    m_virtualDesktopManager->init();
    m_virtualDesktopManager->start();

    // Connect virtual desktop changes to layout switching
    connect(m_virtualDesktopManager.get(), &VirtualDesktopManager::currentDesktopChanged, this, [this](int desktop) {
        // Update all components with current desktop for per-desktop layout lookup
        // NOTE: LayoutManager is the single source of truth for desktop/activity.
        // WindowDragAdaptor reads from LayoutManager directly via resolveLayoutForScreen().
        m_overlayService->setCurrentVirtualDesktop(desktop);
        m_layoutManager->setCurrentVirtualDesktop(desktop);
        if (m_unifiedLayoutController) {
            m_unifiedLayoutController->setCurrentVirtualDesktop(desktop);
        }
        // Pin screens where all autotiled windows are sticky (on all desktops)
        // BEFORE changing the desktop context. This ensures currentKeyForScreen()
        // continues to resolve existing TilingStates for screens managed by the
        // KWin "virtualdesktopsonlyonprimary" script.
        if (m_autotileEngine && m_windowTrackingAdaptor) {
            auto* service = m_windowTrackingAdaptor->service();
            m_autotileEngine->updateStickyScreenPins([service](const QString& windowId) {
                return service->isWindowSticky(windowId);
            });
        }
        // Set engine's desktop context BEFORE updateAutotileScreens() so it
        // resolves TilingStates for the correct desktop. Without this, the
        // engine would look up/create states under the OLD desktop's key.
        if (m_autotileEngine) {
            m_autotileEngine->setCurrentDesktop(desktop);
        }
        // Per-desktop assignments may differ — recompute autotile screens
        updateAutotileScreens();
        // Sync mode, layout filter, and controller state from per-desktop assignments.
        // This ensures ModeTracker, layout filter, and cycling index reflect the
        // new desktop — not the old one's global state.
        syncModeFromAssignments();
        if (m_overlayService->isVisible()) {
            m_overlayService->updateGeometries();
        }

        showDesktopSwitchOsd(desktop, currentActivity());
    });

    // Prune stale TilingState entries and disabled-desktop numbers when desktops are removed
    connect(m_virtualDesktopManager.get(), &VirtualDesktopManager::desktopCountChanged, this, [this](int newCount) {
        // Prune stale disabled-desktop entries (desktop numbers > newCount no longer exist).
        // NOTE: KDE Plasma renumbers desktops when one in the middle is removed (e.g.
        // removing desktop 2 of 4 shifts 3→2 and 4→3). We only prune out-of-range
        // entries here; mid-range renumbering would require tracking which desktop was
        // removed (not available from desktopCountChanged). A future improvement could
        // use KDE's desktop UUIDs instead of 1-based numbers.
        if (m_settings) {
            QStringList disabled = m_settings->disabledDesktops();
            if (pruneDisabledDesktopEntries(disabled, newCount)) {
                m_settings->setDisabledDesktops(disabled);
                m_settings->save();
            }
        }

        if (m_autotileEngine) {
            // Desktop numbers are 1-based. Any state with desktop > newCount is stale.
            // Iterate the engine's own state map to find stale desktops (avoids
            // the arbitrary upper bound of the old newCount+20 sweep).
            QSet<int> staleDesktops;
            for (auto it = m_autotileEngine->screenStates().constBegin();
                 it != m_autotileEngine->screenStates().constEnd(); ++it) {
                if (it.key().desktop > newCount) {
                    staleDesktops.insert(it.key().desktop);
                }
            }
            for (int d : staleDesktops) {
                m_autotileEngine->pruneStatesForDesktop(d);
            }
        }
        // Prune fallback assignment maps
        pruneContextMapsForDesktop(newCount);
    });

    // Set initial virtual desktop on components that maintain their own copy
    // (WindowDragAdaptor reads from LayoutManager directly via resolveLayoutForScreen())
    const int initialDesktop = m_virtualDesktopManager->currentDesktop();
    m_overlayService->setCurrentVirtualDesktop(initialDesktop);
    m_layoutManager->setCurrentVirtualDesktop(initialDesktop);
    if (m_autotileEngine) {
        m_autotileEngine->setCurrentDesktop(initialDesktop);
    }

    // Initialize and start activity manager
    // Connect to VirtualDesktopManager for desktop+activity coordinate lookup
    m_activityManager->setVirtualDesktopManager(m_virtualDesktopManager.get());
    m_activityManager->init();
    if (ActivityManager::isAvailable()) {
        m_activityManager->start();

        // Prune stale TilingState entries and disabled-activity IDs when activities are added/removed
        connect(m_activityManager.get(), &ActivityManager::activitiesChanged, this, [this]() {
            if (!m_activityManager) {
                return;
            }
            const QStringList activities = m_activityManager->activities();
            const QSet<QString> validSet(activities.begin(), activities.end());

            // Prune disabled-activity entries that reference removed activities
            if (m_settings) {
                QStringList disabled = m_settings->disabledActivities();
                if (pruneDisabledActivityEntries(disabled, validSet)) {
                    m_settings->setDisabledActivities(disabled);
                    m_settings->save();
                }
            }

            if (m_autotileEngine) {
                m_autotileEngine->pruneStatesForActivities(activities);
            }
            pruneContextMapsForActivities(validSet);
        });

        // Set initial activity on components that maintain their own copy
        const QString initialActivity = m_activityManager->currentActivity();
        m_overlayService->setCurrentActivity(initialActivity);
        m_layoutManager->setCurrentActivity(initialActivity);
        if (m_autotileEngine) {
            m_autotileEngine->setCurrentActivity(initialActivity);
        }

        // Connect activity changes: update all components
        connect(m_activityManager.get(), &ActivityManager::currentActivityChanged, this,
                [this](const QString& activityId) {
                    m_overlayService->setCurrentActivity(activityId);
                    m_layoutManager->setCurrentActivity(activityId);
                    if (m_unifiedLayoutController) {
                        m_unifiedLayoutController->setCurrentActivity(activityId);
                    }
                    // Pin sticky screens before changing activity context
                    if (m_autotileEngine && m_windowTrackingAdaptor) {
                        auto* service = m_windowTrackingAdaptor->service();
                        m_autotileEngine->updateStickyScreenPins([service](const QString& windowId) {
                            return service->isWindowSticky(windowId);
                        });
                    }
                    // Set engine's activity context BEFORE updateAutotileScreens()
                    if (m_autotileEngine) {
                        m_autotileEngine->setCurrentActivity(activityId);
                    }
                    // Per-activity assignments may differ — recompute autotile screens
                    updateAutotileScreens();
                    // Sync mode, layout filter, and controller state from per-activity assignments.
                    syncModeFromAssignments();
                    if (m_overlayService->isVisible()) {
                        m_overlayService->updateGeometries();
                    }

                    showDesktopSwitchOsd(currentDesktop(), activityId);
                });
    }
}

void Daemon::connectShortcutSignals()
{
    // NOTE: registerShortcuts() is called by Daemon::start() before this method.
    // Do NOT call it again here — it would hit the m_registrationInProgress guard
    // and log a spurious warning.

    // Connect shortcut signals
    // Screen detection: On X11, QCursor::pos() works; on Wayland, background daemons
    // get stale cursor data. resolveShortcutScreenId() handles both by falling back to
    // the screen reported by the KWin effect's windowActivated D-Bus call.
    connect(m_shortcutManager.get(), &ShortcutManager::openSettingsRequested, this, []() {
        // Launch in its own systemd scope so stopping the daemon service
        // doesn't kill the settings app (they'd share a cgroup otherwise).
        if (!QProcess::startDetached(
                QStringLiteral("systemd-run"),
                {QStringLiteral("--user"), QStringLiteral("--scope"), QStringLiteral("plasmazones-settings")})) {
            // Fallback if systemd-run is unavailable
            if (!QProcess::startDetached(QStringLiteral("plasmazones-settings"), {})) {
                qCWarning(lcDaemon) << "Failed to launch plasmazones-settings";
            }
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::openEditorRequested, this, [this]() {
        QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty() && m_unifiedLayoutController) {
            screenId = m_unifiedLayoutController->currentScreenName();
        }
        if (!screenId.isEmpty()) {
            // Pass the effective screen ID directly — the editor handles both
            // physical and virtual screen IDs (VS-aware since v2.9).
            m_layoutAdaptor->openEditorForScreen(screenId);
        } else {
            m_layoutAdaptor->openEditor();
        }
    });
    // Quick layout shortcuts (Meta+1-9)
    connect(m_shortcutManager.get(), &ShortcutManager::quickLayoutRequested, this, [this](int number) {
        if (!m_unifiedLayoutController) {
            return;
        }
        const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "QuickLayout shortcut: no screen info";
            return;
        }
        m_unifiedLayoutController->setCurrentScreenName(screenId);
        if (isScreenLockedForLayoutChange(screenId)) {
            return;
        }
        if (!m_unifiedLayoutController->applyLayoutByNumber(number)) {
            return;
        }
        resnapIfManualMode();
    });

    // Cycle layout shortcuts (Meta+[/])
    connect(m_shortcutManager.get(), &ShortcutManager::previousLayoutRequested, this, [this]() {
        if (m_cycleLayoutDebounce.isValid() && m_cycleLayoutDebounce.elapsed() < kShortcutDebounceMs) {
            return;
        }
        m_cycleLayoutDebounce.restart();
        const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "PreviousLayout shortcut: no screen info";
            return;
        }
        handleCycleLayout(screenId, false);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::nextLayoutRequested, this, [this]() {
        if (m_cycleLayoutDebounce.isValid() && m_cycleLayoutDebounce.elapsed() < kShortcutDebounceMs) {
            return;
        }
        m_cycleLayoutDebounce.restart();
        const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "NextLayout shortcut: no screen info";
            return;
        }
        handleCycleLayout(screenId, true);
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Keyboard Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    // Navigation shortcuts — single code path per operation (handleXxx)
    connect(m_shortcutManager.get(), &ShortcutManager::moveWindowRequested, this, [this](NavigationDirection d) {
        handleMove(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::focusZoneRequested, this, [this](NavigationDirection d) {
        handleFocus(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::pushToEmptyZoneRequested, this, [this]() {
        handlePush();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::restoreWindowSizeRequested, this, [this]() {
        handleRestore();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() {
        handleFloat();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::swapWindowRequested, this, [this](NavigationDirection d) {
        handleSwap(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateWindowsRequested, this, [this](bool cw) {
        handleRotate(cw);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::snapToZoneRequested, this, [this](int n) {
        handleSnap(n);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::cycleWindowsInZoneRequested, this, [this](bool fwd) {
        handleCycle(fwd);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::resnapToNewLayoutRequested, this, [this]() {
        handleResnap();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::snapAllWindowsRequested, this, [this]() {
        handleSnapAll();
    });

    // Layout picker shortcut (interactive layout browser + resnap)
    // Capture screen name at open time so it's still valid after the picker closes.
    connect(m_shortcutManager.get(), &ShortcutManager::layoutPickerRequested, this, [this]() {
        if (!m_unifiedLayoutController) {
            return;
        }
        const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty()) {
            qCDebug(lcDaemon) << "LayoutPicker shortcut: no screen info";
            return;
        }
        m_unifiedLayoutController->setCurrentScreenName(screenId);
        updateLayoutFilterForScreen(screenId);
        m_overlayService->showLayoutPicker(screenId);
    });
    connect(m_overlayService.get(), &OverlayService::layoutPickerSelected, this, [this](const QString& layoutId) {
        if (!m_unifiedLayoutController) {
            return;
        }
        // Check if screen is locked for its current mode
        QString screenId = m_unifiedLayoutController->currentScreenName();
        if (!screenId.isEmpty() && m_layoutManager) {
            int mode = static_cast<int>(m_layoutManager->modeForScreen(screenId, currentDesktop(), currentActivity()));
            if (isCurrentContextLockedForMode(screenId, mode)) {
                showLockedPreviewOsd(screenId);
                return;
            }
        }
        // Screen name was already set when the picker opened.
        if (!m_unifiedLayoutController->applyLayoutById(layoutId)) {
            return;
        }
        resnapIfManualMode();
    });

    // Toggle layout lock shortcut — locks/unlocks current screen at screen-level for current mode
    connect(m_shortcutManager.get(), &ShortcutManager::toggleLayoutLockRequested, this, [this]() {
        const QString screenId = resolveShortcutScreenId(m_windowTrackingAdaptor);
        if (screenId.isEmpty() || !m_settings || !m_layoutManager) {
            return;
        }
        int desktop = currentDesktop();
        QString activity = currentActivity();
        int mode = static_cast<int>(m_layoutManager->modeForScreen(screenId, desktop, activity));
        QString key = QString::number(mode) + QStringLiteral(":") + screenId;
        // Lock at screen-level (desktop=0, activity="") so it applies to all desktops/activities
        // and matches the KCM's screen-level lock button
        bool wasLocked = m_settings->isScreenLocked(key);
        // Block settingsChanged during mutation — the signal triggers a
        // D-Bus relay, and external consumers (settings app) read from disk.
        // If the signal fires before save(), they read stale data.
        {
            QSignalBlocker blocker(m_settings.get());
            m_settings->setScreenLocked(key, !wasLocked);
        }
        m_settings->save();
        // Now that the file is written, notify external consumers
        if (m_settingsAdaptor) {
            Q_EMIT m_settingsAdaptor->settingsChanged();
        }

        if (wasLocked) {
            Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
            if (layout) {
                showLayoutOsd(layout, screenId);
            }
        } else {
            showLockedPreviewOsd(screenId);
        }
        qCInfo(lcDaemon) << "Toggle layout lock:" << (wasLocked ? "unlocked" : "locked") << "screen=" << screenId
                         << "mode=" << mode;
    });
}

void Daemon::pruneContextMapsForDesktop(int maxDesktop)
{
    auto it = m_lastAutotileOrders.begin();
    while (it != m_lastAutotileOrders.end()) {
        if (it.key().desktop > maxDesktop) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void Daemon::pruneContextMapsForActivities(const QSet<QString>& validActivities)
{
    auto it = m_lastAutotileOrders.begin();
    while (it != m_lastAutotileOrders.end()) {
        if (!it.key().activity.isEmpty() && !validActivities.contains(it.key().activity)) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }
}

bool Daemon::isScreenLockedForLayoutChange(const QString& screenId)
{
    if (!m_layoutManager) {
        return false;
    }
    int mode = static_cast<int>(m_layoutManager->modeForScreen(screenId, currentDesktop(), currentActivity()));
    if (isCurrentContextLockedForMode(screenId, mode)) {
        showLockedPreviewOsd(screenId);
        return true;
    }
    return false;
}

void Daemon::handleCycleLayout(const QString& screenId, bool forward)
{
    if (!m_unifiedLayoutController) {
        return;
    }
    m_unifiedLayoutController->setCurrentScreenName(screenId);
    if (isScreenLockedForLayoutChange(screenId)) {
        return;
    }
    updateLayoutFilterForScreen(screenId);
    if (forward) {
        m_unifiedLayoutController->cycleNext();
    } else {
        m_unifiedLayoutController->cyclePrevious();
    }
    resnapIfManualMode();
}

void Daemon::migrateStartupScreenAssignments()
{
    if (!m_windowTrackingAdaptor || !m_screenManager) {
        return;
    }
    const auto vsConfigs = m_settings->virtualScreenConfigs();
    for (auto it = vsConfigs.constBegin(); it != vsConfigs.constEnd(); ++it) {
        if (it.value().hasSubdivisions()) {
            QStringList vsIds = m_screenManager->virtualScreenIdsFor(it.key());
            m_windowTrackingAdaptor->service()->migrateScreenAssignmentsToVirtual(it.key(), vsIds,
                                                                                  m_screenManager.get());
        }
    }
}

void Daemon::pruneAutotileOrdersForRemovedScreens(const QString& physicalScreenId)
{
    const QStringList currentVsIds =
        m_screenManager ? m_screenManager->virtualScreenIdsFor(physicalScreenId) : QStringList();
    QSet<QString> keepIds(currentVsIds.begin(), currentVsIds.end());
    // Also keep the physical ID itself (in case VS config was removed entirely)
    keepIds.insert(physicalScreenId);

    for (auto it = m_lastAutotileOrders.begin(); it != m_lastAutotileOrders.end();) {
        if (VirtualScreenId::extractPhysicalId(it.key().screenId) == physicalScreenId
            && !keepIds.contains(it.key().screenId)) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace PlasmaZones
