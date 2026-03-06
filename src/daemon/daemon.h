// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QTimer>
#include <QHash>
#include <QRect>
#include <memory>

#include "shortcutmanager.h"

namespace PlasmaZones {

class Layout;
class LayoutManager;
class ZoneDetector;
class Settings;
class OverlayService;
class ScreenManager;
class VirtualDesktopManager;
class ActivityManager;
class ShortcutManager;
class LayoutAdaptor;
class SettingsAdaptor;
class OverlayAdaptor;
class ZoneDetectionAdaptor;
class WindowTrackingAdaptor;
class ScreenAdaptor;
class WindowDragAdaptor;
class ModeTracker;
class ZoneSelectorController;
class UnifiedLayoutController;
class AutotileAdaptor;
class AutotileEngine;

/**
 * @brief Main daemon for PlasmaZones
 *
 * Runs in the background managing layouts, zone overlays, KWin D-Bus
 * communication, keyboard shortcuts, and multi-monitor support.
 */
class Daemon : public QObject
{
    Q_OBJECT

public:
    explicit Daemon(QObject* parent = nullptr);
    ~Daemon() override;

    // No singleton - use dependency injection instead

    // Initialization
    bool init();
    void start();
    void stop();

    // Component access
    LayoutManager* layoutManager() const
    {
        return m_layoutManager.get();
    }
    ZoneDetector* zoneDetector() const
    {
        return m_zoneDetector.get();
    }
    Settings* settings() const
    {
        return m_settings.get();
    }
    OverlayService* overlayService() const
    {
        return m_overlayService.get();
    }
    ScreenManager* screenManager() const
    {
        return m_screenManager.get();
    }
    VirtualDesktopManager* virtualDesktopManager() const
    {
        return m_virtualDesktopManager.get();
    }
    ActivityManager* activityManager() const
    {
        return m_activityManager.get();
    }
    ShortcutManager* shortcutManager() const
    {
        return m_shortcutManager.get();
    }

    // Overlay control (delegates to OverlayService)
    Q_INVOKABLE void showOverlay();
    Q_INVOKABLE void hideOverlay();
    Q_INVOKABLE bool isOverlayVisible() const;

    // OSD notifications
    void showLayoutOsd(Layout* layout, const QString& screenName = QString());

private:
    /**
     * @brief Show layout OSD for an autotile algorithm (visual zone preview)
     *
     * Uses showOsdOnLayoutSwitch and osdStyle settings, same as manual layout switch.
     */
    void showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName, const QString& screenName);
    void clearHighlight();

    // ═══════════════════════════════════════════════════════════════════════════
    // Navigation handlers — single code path per operation (DRY/SOLID)
    // Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
    // ═══════════════════════════════════════════════════════════════════════════
    void handleRotate(bool clockwise);
    void handleFloat();
    void handleMove(NavigationDirection direction);
    void handleFocus(NavigationDirection direction);
    void handlePush();
    void handleRestore();
    void handleSwap(NavigationDirection direction);
    void handleSnap(int zoneNumber);
    void handleCycle(bool forward);
    void handleResnap();
    void handleSnapAll();
    void handleFocusMaster();
    void handleSwapWithMaster();
    void handleIncreaseMasterRatio();
    void handleDecreaseMasterRatio();
    void handleIncreaseMasterCount();
    void handleDecreaseMasterCount();
    void handleRetile();
    void connectToKWinScript(); // Shortcuts now handled by ShortcutManager

    // Start-up sub-methods (defined in daemon_start.cpp)
    void connectScreenSignals();
    void connectDesktopActivity();
    void connectShortcutSignals();
    void initializeAutotile();
    void initializeUnifiedController();
    void connectLayoutSignals();
    void connectOverlaySignals();
    void finalizeStartup();

    /**
     * @brief Pre-seed autotile engine with zone-ordered windows for one screen
     *
     * Builds the zone-ordered window list from WTS and passes it to the autotile
     * engine's setInitialWindowOrder(). Used by both per-screen toggle and global
     * snapping→autotile transition.
     *
     * @param screenName Screen connector name
     */
    void seedAutotileOrderForScreen(const QString& screenName);

    /**
     * @brief Handle autotile feature being disabled (clear assignments, restore manual mode)
     */
    void handleAutotileDisabled();

    /**
     * @brief Handle snapping toggle activating autotile mode on all screens
     */
    void handleSnappingToAutotile();

    /** @brief Show layout OSD deferred (avoids blocking on first-time QML compilation) */
    void showLayoutOsdDeferred(const QUuid& layoutId, const QString& screenName);
    /** @brief Show algorithm OSD deferred (avoids blocking on first-time QML compilation) */
    void showAlgorithmOsdDeferred(const QString& algorithmId, const QString& algorithmName, const QString& screenName);

    /**
     * @brief Recompute which screens use autotile from layout assignments
     *
     * Reads all screen assignments via assignmentIdForScreen(), computes
     * which screens have autotile IDs, calls setAutotileScreens() on engine.
     */
    void updateAutotileScreens();

    /** @brief Resnap windows to current layout zones (only in manual/snap mode) */
    void resnapIfManualMode();

    /**
     * @brief Update layout filter on overlay service and unified layout controller
     *
     * Shows both manual and autotile layouts when the feature gate is enabled.
     */
    void updateLayoutFilter();

    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<ZoneDetector> m_zoneDetector;
    std::unique_ptr<OverlayService> m_overlayService;
    std::unique_ptr<ScreenManager> m_screenManager;
    std::unique_ptr<VirtualDesktopManager> m_virtualDesktopManager;
    std::unique_ptr<ActivityManager> m_activityManager;
    std::unique_ptr<ShortcutManager> m_shortcutManager;

    // Domain-specific D-Bus adaptors
    // D-Bus adaptors need a parent (the adapted object); Qt requires it.
    // So we use raw pointers; Qt parent-child system manages their lifetime
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    OverlayAdaptor* m_overlayAdaptor = nullptr; // Overlay visibility only
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr; // Zone detection queries
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr; // Window-zone tracking
    ScreenAdaptor* m_screenAdaptor = nullptr;
    WindowDragAdaptor* m_windowDragAdaptor = nullptr; // Window drag handling

    // Mode tracking
    std::unique_ptr<ModeTracker> m_modeTracker;

    // Unified layout management
    std::unique_ptr<UnifiedLayoutController> m_unifiedLayoutController;

    // Autotile engine
    std::unique_ptr<AutotileEngine> m_autotileEngine;
    AutotileAdaptor* m_autotileAdaptor = nullptr;

    // Desktop/activity resolution helpers (DRY — used by multiple handlers)
    int currentDesktop() const;
    QString currentActivity() const;

    /** @brief Resolve algorithm ID with fallback: last used → settings → default */
    QString resolveAlgorithmId() const;

    bool m_running = false;
    bool m_suppressResnapOsd = false;

    // State tracking for settingsChanged delta detection (replaces individual signal handlers)
    // Initialized from m_settings in init() before settingsChanged is connected.
    // Header defaults are safe no-ops: both false means "no prior state" so the
    // first settingsChanged won't detect a spurious toggle.
    bool m_prevSnappingEnabled = false;
    bool m_prevAutotileEnabled = false;

    // Geometry update debouncing to prevent cascade of redundant recalculations
    QTimer m_geometryUpdateTimer;
    QHash<QString, QRect> m_pendingGeometryUpdates;
    void processPendingGeometryUpdates();

    // After geometry updates settle, request KWin effect to re-apply window positions (panel editor fix)
    QTimer m_reapplyGeometriesTimer;
};

} // namespace PlasmaZones
