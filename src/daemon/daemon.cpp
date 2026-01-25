// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"
#include "overlayservice.h"
#include "../core/layoutmanager.h"
#include "../core/zonedetector.h"
#include "../core/screenmanager.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "shortcutmanager.h"
#include "../config/settings.h"
#include "../dbus/layoutadaptor.h"
#include "../dbus/settingsadaptor.h"
#include "../dbus/overlayadaptor.h"
#include "../dbus/zonedetectionadaptor.h"
#include "../dbus/windowtrackingadaptor.h"
#include "../dbus/screenadaptor.h"
#include "../dbus/windowdragadaptor.h"

#include <QGuiApplication>
#include <QScreen>
#include <QCursor>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusError>
#include <QThread>
#include <QProcess>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KConfigGroup>

namespace PlasmaZones {

Daemon::Daemon(QObject* parent)
    : QObject(parent)
    // Don't pass 'this' as parent for unique_ptr-managed objects.
    // unique_ptr owns lifetime; a Qt parent would double-free.
    , m_layoutManager(std::make_unique<LayoutManager>(nullptr))
    , m_zoneDetector(std::make_unique<ZoneDetector>(nullptr))
    , m_settings(std::make_unique<Settings>(nullptr))
    , m_overlayService(std::make_unique<OverlayService>(nullptr))
    , m_screenManager(std::make_unique<ScreenManager>(nullptr))
    , m_virtualDesktopManager(std::make_unique<VirtualDesktopManager>(m_layoutManager.get(), nullptr))
    , m_activityManager(std::make_unique<ActivityManager>(m_layoutManager.get(), nullptr))
    , m_shortcutManager(std::make_unique<ShortcutManager>(m_settings.get(), m_layoutManager.get(), nullptr))
{
    // Configure geometry update debounce timer
    // This prevents cascading recalculations when multiple geometry changes occur rapidly
    m_geometryUpdateTimer.setSingleShot(true);
    m_geometryUpdateTimer.setInterval(50); // 50ms debounce - fast enough to feel responsive
    connect(&m_geometryUpdateTimer, &QTimer::timeout, this, &Daemon::processPendingGeometryUpdates);
}

Daemon::~Daemon()
{
    stop();
}

bool Daemon::init()
{
    // Load settings
    m_settings->load();

    m_layoutManager->setSettings(m_settings.get());
    // Load layouts (pass defaultLayoutId so initial active uses Settings default when set)
    m_layoutManager->loadLayouts(m_settings->defaultLayoutId());
    m_layoutManager->loadAssignments();

    // Configure overlay service with settings, layout manager, and active layout
    m_overlayService->setSettings(m_settings.get());
    m_overlayService->setLayoutManager(m_layoutManager.get());
    if (m_layoutManager->activeLayout()) {
        m_overlayService->setLayout(m_layoutManager->activeLayout());
        m_zoneDetector->setLayout(m_layoutManager->activeLayout());
        qCDebug(lcDaemon) << "Overlay configured with layout:" << m_layoutManager->activeLayout()->name()
                          << "zones:" << m_layoutManager->activeLayout()->zoneCount();
    } else {
        qCWarning(lcDaemon) << "No active layout available for overlay";
    }

    // Connect layout changes to zone detector and overlay service
    // activeLayoutChanged fires when the global active layout changes; layoutAssigned
    // fires for per-screen assignments. We handle both but avoid redundant recalculations.
    connect(m_layoutManager.get(), &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
        if (layout) {
            // Recalculate zone geometries for all screens when global layout changes
            for (auto* screen : m_screenManager->screens()) {
                QRect availableGeom = ScreenManager::actualAvailableGeometry(screen);
                layout->recalculateZoneGeometries(availableGeom);
            }
        }
        m_zoneDetector->setLayout(layout);
        m_overlayService->updateLayout(layout);
    });

    // Connect per-screen layout assignments
    // Only update if this is a DIFFERENT layout than the active one
    // (to avoid double-processing when both signals fire for the same layout)
    connect(m_layoutManager.get(), &LayoutManager::layoutAssigned, this,
            [this](const QString& screenName, Layout* layout) {
                if (!layout) {
                    return;
                }
                // Skip if this layout is already the active layout
                // (activeLayoutChanged handler already processed it for all screens)
                if (layout == m_layoutManager->activeLayout()) {
                    return;
                }
                // This is a screen-specific layout different from the active one
                // Only recalculate for the specific screen
                QScreen* screen = m_screenManager->screenByName(screenName);
                if (screen) {
                    QRect availableGeom = ScreenManager::actualAvailableGeometry(screen);
                    layout->recalculateZoneGeometries(availableGeom);
                }
                // Note: We don't change zone detector or overlay here since
                // they work with the active layout, not per-screen layouts
            });

    // Connect settings changes to overlay service
    connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());
    });

    // Connect overlay visibility to daemon signal
    connect(m_overlayService.get(), &OverlayService::visibilityChanged, this, &Daemon::overlayVisibilityChanged);

    // Initialize domain-specific D-Bus adaptors (SRP)
    // Each adaptor has a single responsibility and its own D-Bus interface
    // D-Bus adaptors use raw new; Qt parent-child manages their lifetime.
    m_layoutAdaptor = new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), this);
    m_settingsAdaptor = new SettingsAdaptor(m_settings.get(), this);

    // Overlay adaptor - overlay visibility and highlighting (SRP - kept for backward compatibility)
    m_overlayAdaptor =
        new OverlayAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(), m_settings.get(), this);

    // Zone detection adaptor - zone detection queries (SRP)
    m_zoneDetectionAdaptor =
        new ZoneDetectionAdaptor(m_zoneDetector.get(), m_layoutManager.get(), m_settings.get(), this);

    // Window tracking adaptor - window-zone assignments (SRP)
    m_windowTrackingAdaptor = new WindowTrackingAdaptor(m_layoutManager.get(), m_zoneDetector.get(), m_settings.get(),
                                                        m_virtualDesktopManager.get(), this);

    m_screenAdaptor = new ScreenAdaptor(this);

    // Window drag adaptor - handles drag events from KWin script (SRP)
    // All drag logic (modifiers, zones, snapping) handled here
    m_windowDragAdaptor = new WindowDragAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                                m_settings.get(), m_windowTrackingAdaptor, this);

    // Zone selector methods are called directly from WindowDragAdaptor; QDBusAbstractAdaptor
    // signals are for D-Bus, not Qt connections.

    // Register D-Bus service and object with error handling and retry logic
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCCritical(lcDaemon) << "Cannot connect to session D-Bus - daemon cannot function without D-Bus";
        return false;
    }

    // Retry D-Bus service registration (with exponential backoff)
    const int maxRetries = 3;
    bool serviceRegistered = false;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (bus.registerService(QString(DBus::ServiceName))) {
            serviceRegistered = true;
            break;
        }

        QDBusError error = bus.lastError();
        if (error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::NoReply) {
            // Transient error - retry
            if (attempt < maxRetries - 1) {
                int delayMs = 1000 * (attempt + 1); // Exponential backoff: 1s, 2s, 3s
                qCWarning(lcDaemon) << "Failed to register D-Bus service (attempt" << (attempt + 1) << "/" << maxRetries
                                    << "):" << error.message() << "- retrying in" << delayMs << "ms";
                QThread::msleep(delayMs);
                continue;
            }
        }

        // Non-retryable error or max retries reached
        qCCritical(lcDaemon) << "Failed to register D-Bus service:" << DBus::ServiceName << "Error:" << error.message()
                             << "Type:" << error.type();
        return false;
    }

    if (!serviceRegistered) {
        qCCritical(lcDaemon) << "Failed to register D-Bus service after" << maxRetries << "attempts";
        return false;
    }

    // Register D-Bus object (no retry needed - service is already registered)
    if (!bus.registerObject(QString(DBus::ObjectPath), this)) {
        QDBusError error = bus.lastError();
        qCCritical(lcDaemon) << "Failed to register D-Bus object:" << DBus::ObjectPath << "Error:" << error.message();
        // Cleanup: unregister service if object registration fails
        bus.unregisterService(QString(DBus::ServiceName));
        return false;
    }

    qCInfo(lcDaemon) << "D-Bus service registered successfully:" << DBus::ServiceName << "at" << DBus::ObjectPath;

    // Connect overlay adaptor signals to daemon overlay control
    connect(m_overlayAdaptor, &OverlayAdaptor::overlayVisibilityChanged, this, [this](bool visible) {
        if (visible) {
            showOverlay();
        } else {
            hideOverlay();
        }
    });

    // Connect zone detection to overlay updates
    connect(m_zoneDetectionAdaptor, &ZoneDetectionAdaptor::zoneDetected, this,
            [this](const QString& zoneId, const QString& geometry) {
                Q_UNUSED(zoneId)
                Q_UNUSED(geometry)
                // Update overlay when zone is detected
                m_overlayService->updateGeometries();
            });

    return true;
}

void Daemon::start()
{
    if (m_running) {
        return;
    }

    // Initialize and start screen manager (SRP: centralized screen handling)
    m_screenManager->init();
    m_screenManager->start();

    // Initialize and start virtual desktop manager (SRP: virtual desktop handling)
    m_virtualDesktopManager->init();
    m_virtualDesktopManager->start();

    // Connect virtual desktop changes to layout switching
    connect(m_virtualDesktopManager.get(), &VirtualDesktopManager::currentDesktopChanged, this, [this](int desktop) {
        // Update overlay service with current desktop for per-desktop layout lookup
        m_overlayService->setCurrentVirtualDesktop(desktop);
        // Layout switching is handled automatically by VirtualDesktopManager
        // Just ensure overlay is updated
        if (m_overlayService->isVisible()) {
            m_overlayService->updateGeometries();
        }
    });

    // Set initial virtual desktop on overlay service
    m_overlayService->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());

    // Initialize and start activity manager (SRP: activity handling)
    m_activityManager->init();
    if (ActivityManager::isAvailable()) {
        m_activityManager->start();

        // Connect activity changes to layout switching
        connect(m_activityManager.get(), &ActivityManager::currentActivityChanged, this,
                [this](const QString& activityId) {
                    Q_UNUSED(activityId)
                    // Layout switching is handled automatically by ActivityManager
                    // Just ensure overlay is updated
                    if (m_overlayService->isVisible()) {
                        m_overlayService->updateGeometries();
                    }
                });
    }

    // Connect screen manager signals
    connect(m_screenManager.get(), &ScreenManager::screenAdded, this, [this](QScreen* screen) {
        m_overlayService->handleScreenAdded(screen);
        if (m_layoutManager->activeLayout()) {
            // Use actualAvailableGeometry() to position zones within usable area (excluding panels/taskbars)
            m_layoutManager->activeLayout()->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));
        }
    });

    connect(m_screenManager.get(), &ScreenManager::screenRemoved, this, [this](QScreen* screen) {
        m_overlayService->handleScreenRemoved(screen);
    });

    connect(m_screenManager.get(), &ScreenManager::screenGeometryChanged, this,
            [this](QScreen* screen, const QRect& geometry) {
                Q_UNUSED(geometry)
                // Queue geometry update with debouncing to avoid cascade
                QRect availableGeom = ScreenManager::actualAvailableGeometry(screen);
                m_pendingGeometryUpdates[screen->name()] = availableGeom;
                m_geometryUpdateTimer.start();
            });

    // Connect to available geometry changes (panels added/removed/resized)
    // This is reactive - the sensor windows automatically track panel changes
    // Uses debouncing to coalesce rapid changes into a single update
    connect(m_screenManager.get(), &ScreenManager::availableGeometryChanged, this,
            [this](QScreen* screen, const QRect& availableGeometry) {
                // Queue geometry update with debouncing
                // Multiple rapid changes will be coalesced into a single update
                m_pendingGeometryUpdates[screen->name()] = availableGeometry;
                m_geometryUpdateTimer.start();
            });

    // Don't pre-create overlay windows at startup. On Wayland with LayerShellQt
    // this can cause visibility issues. Create on-demand in show() instead,
    // which also avoids the overlay flashing during login.
    qCInfo(lcDaemon) << "Overlay service ready -" << m_screenManager->screens().count()
                     << "screens available (windows created on-demand)";

    // Register global shortcuts via ShortcutManager (SRP)
    m_shortcutManager->registerShortcuts();

    // Connect shortcut signals
    connect(m_shortcutManager.get(), &ShortcutManager::openEditorRequested, this, [this]() {
        // Open editor for the screen under the cursor (or primary screen as fallback)
        QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = Utils::primaryScreen();
        }
        if (screen) {
            m_layoutAdaptor->openEditorForScreen(screen->name());
        } else {
            m_layoutAdaptor->openEditor();
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::quickLayoutRequested, this, [this](int number) {
        auto* screen = Utils::primaryScreen();
        m_layoutManager->applyQuickLayout(number, screen ? screen->name() : QString());
        // Show OSD with the new layout name (if enabled)
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            if (auto* layout = m_layoutManager->activeLayout()) {
                showLayoutOsd(layout->name());
            }
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::previousLayoutRequested, this, [this]() {
        auto* screen = Utils::primaryScreen();
        m_layoutManager->cycleToPreviousLayout(screen ? screen->name() : QString());
        // Show OSD with the new layout name (if enabled)
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            if (auto* layout = m_layoutManager->activeLayout()) {
                showLayoutOsd(layout->name());
            }
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::nextLayoutRequested, this, [this]() {
        auto* screen = Utils::primaryScreen();
        m_layoutManager->cycleToNextLayout(screen ? screen->name() : QString());
        // Show OSD with the new layout name (if enabled)
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            if (auto* layout = m_layoutManager->activeLayout()) {
                showLayoutOsd(layout->name());
            }
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Phase 1 Keyboard Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    // Move window to adjacent zone shortcuts
    connect(m_shortcutManager.get(), &ShortcutManager::moveWindowRequested, this,
            [this](NavigationDirection direction) {
                QString dirStr;
                switch (direction) {
                case NavigationDirection::Left:
                    dirStr = QStringLiteral("left");
                    break;
                case NavigationDirection::Right:
                    dirStr = QStringLiteral("right");
                    break;
                case NavigationDirection::Up:
                    dirStr = QStringLiteral("up");
                    break;
                case NavigationDirection::Down:
                    dirStr = QStringLiteral("down");
                    break;
                default:
                    qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
                    return;
                }
                m_windowTrackingAdaptor->moveWindowToAdjacentZone(dirStr);
            });

    // Focus navigation to adjacent zone shortcuts
    connect(m_shortcutManager.get(), &ShortcutManager::focusZoneRequested, this, [this](NavigationDirection direction) {
        QString dirStr;
        switch (direction) {
        case NavigationDirection::Left:
            dirStr = QStringLiteral("left");
            break;
        case NavigationDirection::Right:
            dirStr = QStringLiteral("right");
            break;
        case NavigationDirection::Up:
            dirStr = QStringLiteral("up");
            break;
        case NavigationDirection::Down:
            dirStr = QStringLiteral("down");
            break;
        default:
            qCWarning(lcDaemon) << "Unknown focus navigation direction:" << static_cast<int>(direction);
            return;
        }
        m_windowTrackingAdaptor->focusAdjacentZone(dirStr);
    });

    // Push to empty zone shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::pushToEmptyZoneRequested, this, [this]() {
        m_windowTrackingAdaptor->pushToEmptyZone();
    });

    // Restore window size shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::restoreWindowSizeRequested, this, [this]() {
        m_windowTrackingAdaptor->restoreWindowSize();
    });

    // Toggle window float shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() {
        m_windowTrackingAdaptor->toggleWindowFloat();
    });

    // Connect to KWin script
    connectToKWinScript();

    m_running = true;
    Q_EMIT started();

    // Signal that daemon is fully initialized and ready for queries
    Q_EMIT m_layoutAdaptor->daemonReady();
}

void Daemon::stop()
{
    if (!m_running) {
        return;
    }

    // Hide overlay
    hideOverlay();

    // Save state
    m_layoutManager->saveLayouts();
    m_layoutManager->saveAssignments();
    m_settings->save();

    m_running = false;
    Q_EMIT stopped();
}

void Daemon::showOverlay()
{
    m_overlayService->show();
}

void Daemon::hideOverlay()
{
    clearHighlight();
    m_overlayService->hide();
}

bool Daemon::isOverlayVisible() const
{
    return m_overlayService->isVisible();
}

void Daemon::updateHighlight(const QPointF& cursorPos)
{
    if (!m_overlayService->isVisible()) {
        return;
    }

    auto result = m_zoneDetector->detectZone(cursorPos);
    if (result.primaryZone) {
        m_zoneDetector->highlightZone(result.primaryZone);
    } else {
        m_zoneDetector->clearHighlights();
    }

    // Trigger overlay update to show highlighted zones
    m_overlayService->updateGeometries();
}

void Daemon::clearHighlight()
{
    m_zoneDetector->clearHighlights();
}

void Daemon::showLayoutOsd(const QString& layoutName)
{
    // Use KDE Plasma's OSD service for native-feeling notifications
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                                       QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));

    // Arguments: icon name, text to display
    // Using PlasmaZones app icon with clear "Zone Layout:" prefix
    // Use i18n for translation support
    QString displayText = i18n("Zone Layout: %1", layoutName);
    msg << QStringLiteral("plasmazones") << displayText;

    QDBusConnection::sessionBus().asyncCall(msg);
    qCDebug(lcDaemon) << "Showing OSD for layout:" << layoutName;
}

// Screen management now handled by ScreenManager (SRP)
// Shortcut management now handled by ShortcutManager (SRP)
// Signals are connected in start() method

void Daemon::connectToKWinScript()
{
    // The KWin script will call us via D-Bus
    // We just need to be ready to receive calls

    // Monitor for KWin script connection
    // The script will call getActiveLayout() on startup
}

void Daemon::processPendingGeometryUpdates()
{
    if (m_pendingGeometryUpdates.isEmpty()) {
        return;
    }

    // Process all pending geometry updates in a single batch
    // This prevents N×M work when multiple screens change simultaneously
    for (auto it = m_pendingGeometryUpdates.constBegin(); it != m_pendingGeometryUpdates.constEnd(); ++it) {
        const QString& screenName = it.key();
        const QRect& availableGeometry = it.value();

        qCDebug(lcDaemon) << "Processing geometry update for" << screenName
                          << "availableGeometry:" << availableGeometry;

        // Recalculate zone geometries for active layout
        if (m_layoutManager->activeLayout()) {
            m_layoutManager->activeLayout()->recalculateZoneGeometries(availableGeometry);
        }

        // Also update screen-specific layout if different from active
        if (Layout* screenLayout = m_layoutManager->layoutForScreen(screenName)) {
            if (screenLayout != m_layoutManager->activeLayout()) {
                screenLayout->recalculateZoneGeometries(availableGeometry);
            }
        }
    }

    m_pendingGeometryUpdates.clear();

    // Single overlay update after all geometry recalculations
    m_overlayService->updateGeometries();
}

} // namespace PlasmaZones
