// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../daemon.h"
#include "helpers.h"
#include "../overlayservice.h"
#include "../unifiedlayoutcontroller.h"
#include "../shortcutmanager.h"
#include "../../core/layoutmanager.h"
#include "../../core/layoutworker/layoutcomputeservice.h"
#include "../../core/screenmanagerservice.h"
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
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include "../../snap/SnapEngine.h"
#include "../../core/shaderregistry.h"
#include <PhosphorZones/ZoneDetector.h>
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

    // ─────────────────────────────────────────────────────────────────────
    // Settings → ScreenManager observer wiring
    //
    // Settings is the single source of truth for virtual screen configs.
    // ScreenManager keeps a cache (for fast geometry lookups) that is rebuilt
    // from Settings whenever Settings::virtualScreenConfigsChanged fires —
    // either from disk reload (notifyReload), in-process mutation (D-Bus
    // ScreenAdaptor::setVirtualScreenConfig writes here, not directly to
    // ScreenManager), or programmatic settings changes.
    //
    // Both connections fire ScreenManager::virtualScreensChanged(physId) for
    // each changed entry, which the downstream handler below picks up to do
    // migration / autotile / resnap work.
    // ─────────────────────────────────────────────────────────────────────
    connect(m_settings.get(), &Settings::virtualScreenConfigsChanged, this, [this]() {
        m_screenManager->refreshVirtualConfigs(m_settings->virtualScreenConfigs());
    });
    // Initial sync: pull whatever Settings::load() already populated.
    m_screenManager->refreshVirtualConfigs(m_settings->virtualScreenConfigs());

    // React to ScreenManager VS cache changes (driven by the observer above
    // OR by direct test calls). Delegates to onVirtualScreensReconfigured
    // which migrates window assignments, refreshes autotile, resnaps
    // windows, and schedules downstream geometry updates. NOTE: this
    // handler no longer writes back to Settings — Settings is now the
    // source, not the sink.
    connect(m_screenManager.get(), &ScreenManager::virtualScreensChanged, this, &Daemon::onVirtualScreensReconfigured);
    connect(m_screenManager.get(), &ScreenManager::virtualScreenRegionsChanged, this,
            &Daemon::onVirtualScreenRegionsChanged);

    // Connect screen manager signals
    connect(m_screenManager.get(), &ScreenManager::screenAdded, this, [this](QScreen* screen) {
        // Invalidate cached EDID serial so a fresh sysfs read happens for this connector
        // (handles the case where EDID wasn't available during very early startup)
        Utils::invalidateEdidCache(screen->name());
        m_overlayService->handleScreenAdded(screen);
        // Recalculate zone geometries for all effective screen IDs on this physical screen.
        // Note: VS cache restoration on screen re-add is no longer needed —
        // ScreenManager::onScreenRemoved no longer wipes m_virtualConfigs, so
        // the entry survives a disconnect and is reused as-is when the screen
        // comes back. Settings is the source of truth and pushes updates via
        // refreshVirtualConfigs() in response to its own change signal.
        const QString physId = Utils::screenIdentifier(screen);
        const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physId);
        const int desktop = m_virtualDesktopManager->currentDesktop();
        const QString activity =
            m_activityManager && ActivityManager::isAvailable() ? m_activityManager->currentActivity() : QString();
        for (const QString& sid : vsIds) {
            PhosphorZones::Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
            if (screenLayout) {
                LayoutComputeService::recalculateSync(screenLayout,
                                                      GeometryUtils::effectiveScreenGeometry(screenLayout, sid));
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
        for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
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
        // Desktop switch invalidates the TilingStateKey context — cancel any
        // active drag-insert preview before the engine's desktop changes,
        // otherwise cancel/commit would operate on the wrong PhosphorTiles::TilingState.
        if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
            m_autotileEngine->cancelDragInsertPreview();
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

    // Prune stale PhosphorTiles::TilingState entries and disabled-desktop numbers when desktops are removed
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
            // Desktop numbers are 1-based. Any state with desktop > newCount
            // is stale. desktopsWithActiveState() returns the set of desktops
            // currently holding tiling state — we filter that for anything
            // past the new count and prune, avoiding the arbitrary upper
            // bound of the old newCount+20 sweep.
            const QSet<int> active = m_autotileEngine->desktopsWithActiveState();
            for (int d : active) {
                if (d > newCount) {
                    m_autotileEngine->pruneStatesForDesktop(d);
                }
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

        // Prune stale PhosphorTiles::TilingState entries and disabled-activity IDs when activities are added/removed
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
                    // Activity switch invalidates the TilingStateKey context — cancel
                    // any active drag-insert preview before the engine's activity changes.
                    if (m_autotileEngine && m_autotileEngine->hasDragInsertPreview()) {
                        m_autotileEngine->cancelDragInsertPreview();
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
    connect(m_shortcutManager.get(), &ShortcutManager::swapVirtualScreenRequested, this, [this](NavigationDirection d) {
        handleSwapVirtualScreen(d);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateVirtualScreensRequested, this, [this](bool cw) {
        handleRotateVirtualScreens(cw);
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

    // PhosphorZones::Layout picker shortcut (interactive layout browser + resnap)
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
            PhosphorZones::Layout* layout = m_layoutManager->resolveLayoutForScreen(screenId);
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
        if (PhosphorIdentity::VirtualScreenId::extractPhysicalId(it.key().screenId) == physicalScreenId
            && !keepIds.contains(it.key().screenId)) {
            it = m_lastAutotileOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void Daemon::onVirtualScreensReconfigured(const QString& physicalScreenId)
{
    // m_screenManager / m_layoutManager / m_virtualDesktopManager are
    // unique_ptrs constructed in the Daemon ctor's initializer list and
    // never nulled — by the time this signal handler fires they are valid.
    // m_windowTrackingAdaptor is constructed later in init() and may be null
    // if the signal somehow fires before construction (e.g. early Settings
    // load); guard those uses individually.
    const Phosphor::Screens::VirtualScreenConfig config = m_screenManager->virtualScreenConfig(physicalScreenId);

    // Recalculate zone geometries inline for the affected screens FIRST so
    // that any PhosphorTiles::TilingState created by the upcoming updateAutotileScreens
    // call (and the resnap below) reads fresh zone bounds. The screenAdded
    // handler does the same inline recalc for newly-added physical screens.
    const int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    const QString activity =
        m_activityManager && ActivityManager::isAvailable() ? m_activityManager->currentActivity() : QString();
    const QStringList affectedScreenIds = config.hasSubdivisions()
        ? m_screenManager->virtualScreenIdsFor(physicalScreenId)
        : QStringList{physicalScreenId};
    for (const QString& sid : affectedScreenIds) {
        PhosphorZones::Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
        if (screenLayout) {
            LayoutComputeService::recalculateSync(screenLayout,
                                                  GeometryUtils::effectiveScreenGeometry(screenLayout, sid));
        }
    }

    // Clear stale resnap buffer — screen IDs have changed.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->service()->clearResnapBuffer();
    }

    // Migrate window screen assignments to the new VS IDs (when subdivisions
    // exist). Migration is a no-op for windows already on a valid VS ID in
    // the new config — those keep their stored screen.
    if (config.hasSubdivisions() && m_windowTrackingAdaptor) {
        const QStringList vsIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
        m_windowTrackingAdaptor->service()->migrateScreenAssignmentsToVirtual(physicalScreenId, vsIds,
                                                                              m_screenManager.get());
    }

    // Prune stale autotile order entries for old virtual screen IDs.
    pruneAutotileOrdersForRemovedScreens(physicalScreenId);

    // Re-derive the autotile screen set so the engine picks up new virtual
    // screen IDs (or drops removed ones) and creates/destroys TilingStates
    // accordingly. Without this, re-splitting a physical screen whose
    // assignments already exist on the VS IDs leaves the engine unaware of
    // the screens — onScreenGeometryChanged early-returns and no tiling
    // happens until something else (e.g. an assignment change) fires
    // layoutAssigned → updateAutotileScreens.
    updateAutotileScreens();

    // Resnap windows on this physical screen and any of its virtual children
    // to their stored zones. Uses calculateResnapFromCurrentAssignments which
    // is NOT gated on keepWindowsInZonesOnResolutionChange — VS reconfiguration
    // is user-initiated, not a passive resolution change. The physId filter is
    // VS-aware via Utils::belongsToPhysicalScreen. The vs_reconfigure-tagged
    // variant suppresses the kwin-effect snap-assist continuation so users
    // don't get a thumbnail picker popping up after every VS swap/rotate.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->resnapForVirtualScreenReconfigure(physicalScreenId);
    }

    // Trigger debounced geometry recalculation for the rest of the system
    // (overlays, panel requery, autotile retile). Reuse the same debounced
    // path as physical screen geometry changes.
    if (resolvePhysicalScreen(physicalScreenId)) {
        m_geometryUpdatePending = true;
        m_geometryUpdateTimer.start();
    }
}

void Daemon::onVirtualScreenRegionsChanged(const QString& physicalScreenId)
{
    // VS ID set is unchanged (swap/rotate/boundary resize). The heavy topology
    // work in onVirtualScreensReconfigured is all no-ops for this case, so we
    // short-circuit to just the steps that actually matter:
    //   1. Recompute zone geometries for each affected VS layout.
    //   2. Kick the snap-mode resnap (tagged vs_reconfigure → no snap-assist).
    // The autotile retile is handled by AutotileEngine's own
    // virtualScreenRegionsChanged handler — we deliberately do NOT call
    // updateAutotileScreens() here, because that would force a second retile
    // pass on top of the engine's own, producing the visible "move then
    // retile" double-movement reported on VS swap/rotate.

    const int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    const QString activity =
        m_activityManager && ActivityManager::isAvailable() ? m_activityManager->currentActivity() : QString();
    const QStringList affectedScreenIds = m_screenManager->virtualScreenIdsFor(physicalScreenId);
    for (const QString& sid : affectedScreenIds) {
        PhosphorZones::Layout* screenLayout = m_layoutManager->layoutForScreen(sid, desktop, activity);
        if (screenLayout) {
            LayoutComputeService::recalculateSync(screenLayout,
                                                  GeometryUtils::effectiveScreenGeometry(screenLayout, sid));
        }
    }

    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->service()->clearResnapBuffer();
        m_windowTrackingAdaptor->resnapForVirtualScreenReconfigure(physicalScreenId);
    }
}

} // namespace PlasmaZones
