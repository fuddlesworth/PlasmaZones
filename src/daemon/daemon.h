// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QGuiApplication>
#include <QTimer>
#include <QHash>
#include <QRect>
#include <memory>

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
class AutotileAdaptor;
class AutotileEngine;
class WindowTrackingService;
class ModeTracker;
class ContextAwareShortcutRouter;
class ZoneSelectorController;

/**
 * @brief Main daemon for PlasmaZones
 *
 * The daemon runs in the background and handles:
 * - Layout management and persistence
 * - Zone overlay rendering via Wayland layer-shell
 * - Communication with KWin script via D-Bus
 * - Global keyboard shortcut handling
 * - Multi-monitor support
 *
 * Note: This class does NOT use the singleton pattern. Create instances
 * where needed and pass via dependency injection.
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

    // Zone highlighting
    Q_INVOKABLE void updateHighlight(const QPointF& cursorPos);
    Q_INVOKABLE void clearHighlight();

    // OSD notifications
    void showLayoutOsd(Layout* layout);
    void showAutotileOsd(const QString& algorithmId);

Q_SIGNALS:
    void overlayVisibilityChanged(bool visible);
    void started();
    void stopped();

private:
    void connectToKWinScript(); // Shortcuts now handled by ShortcutManager (SRP)

    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<ZoneDetector> m_zoneDetector;
    std::unique_ptr<Settings> m_settings;
    std::unique_ptr<OverlayService> m_overlayService;
    std::unique_ptr<ScreenManager> m_screenManager;
    std::unique_ptr<VirtualDesktopManager> m_virtualDesktopManager;
    std::unique_ptr<ActivityManager> m_activityManager;
    std::unique_ptr<ShortcutManager> m_shortcutManager;

    // Domain-specific D-Bus adaptors (SRP - each has single responsibility)
    // D-Bus adaptors need a parent (the adapted object); Qt requires it.
    // So we use raw pointers; Qt parent-child system manages their lifetime
    LayoutAdaptor* m_layoutAdaptor = nullptr;
    SettingsAdaptor* m_settingsAdaptor = nullptr;
    OverlayAdaptor* m_overlayAdaptor = nullptr; // Overlay visibility only
    ZoneDetectionAdaptor* m_zoneDetectionAdaptor = nullptr; // Zone detection queries
    WindowTrackingAdaptor* m_windowTrackingAdaptor = nullptr; // Window-zone tracking
    ScreenAdaptor* m_screenAdaptor = nullptr;
    WindowDragAdaptor* m_windowDragAdaptor = nullptr; // Window drag handling
    AutotileAdaptor* m_autotileAdaptor = nullptr; // Autotiling control

    // Autotiling engine and service
    std::unique_ptr<WindowTrackingService> m_windowTrackingService;
    std::unique_ptr<AutotileEngine> m_autotileEngine;

    // Mode tracking and context-aware shortcuts
    std::unique_ptr<ModeTracker> m_modeTracker;
    std::unique_ptr<ContextAwareShortcutRouter> m_shortcutRouter;

    // Unified layout cycling (manual layouts + autotile algorithms)
    struct UnifiedLayoutEntry {
        QString id;        // Layout UUID or "autotile:<algorithm-id>"
        QString name;      // Display name
        bool isAutotile;   // True for autotile algorithms
    };
    QVector<UnifiedLayoutEntry> buildUnifiedLayoutList() const;
    void applyUnifiedLayout(int index);
    void cycleUnifiedLayout(bool forward);
    int m_currentUnifiedLayoutIndex = -1;

    bool m_running = false;

    // Geometry update debouncing to prevent cascade of redundant recalculations
    QTimer m_geometryUpdateTimer;
    QHash<QString, QRect> m_pendingGeometryUpdates;
    void processPendingGeometryUpdates();
};

} // namespace PlasmaZones
