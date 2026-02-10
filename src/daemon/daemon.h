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
class WindowTrackingService;
class AutoTileService;
class ModeTracker;
class ZoneSelectorController;
class UnifiedLayoutController;

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

    // Zone highlighting
    Q_INVOKABLE void updateHighlight(const QPointF& cursorPos);
    Q_INVOKABLE void clearHighlight();

    // OSD notifications
    void showLayoutOsd(Layout* layout, const QString& screenName = QString());

Q_SIGNALS:
    void overlayVisibilityChanged(bool visible);
    void started();
    void stopped();

private:
    void connectToKWinScript(); // Shortcuts now handled by ShortcutManager

    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<ZoneDetector> m_zoneDetector;
    std::unique_ptr<Settings> m_settings;
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

    std::unique_ptr<WindowTrackingService> m_windowTrackingService;
    std::unique_ptr<AutoTileService> m_autoTileService;

    // Mode tracking
    std::unique_ptr<ModeTracker> m_modeTracker;

    // Unified layout management
    std::unique_ptr<UnifiedLayoutController> m_unifiedLayoutController;

    bool m_running = false;

    // Geometry update debouncing to prevent cascade of redundant recalculations
    QTimer m_geometryUpdateTimer;
    QHash<QString, QRect> m_pendingGeometryUpdates;
    void processPendingGeometryUpdates();
};

} // namespace PlasmaZones
