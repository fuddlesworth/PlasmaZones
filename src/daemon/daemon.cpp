// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"
#include "overlayservice.h"
#include "modetracker.h"
#include "contextawareshortcutrouter.h"
#include "unifiedlayoutcontroller.h"
#include "zoneselectorcontroller.h"
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
#include "../dbus/autotileadaptor.h"
#include "../autotile/AutotileEngine.h"
#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"
#include "../core/windowtrackingservice.h"
#include "../core/shaderregistry.h"

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

namespace {
// Helper function to convert NavigationDirection to string
QString navigationDirectionToString(NavigationDirection direction)
{
    switch (direction) {
    case NavigationDirection::Left:
        return QStringLiteral("left");
    case NavigationDirection::Right:
        return QStringLiteral("right");
    case NavigationDirection::Up:
        return QStringLiteral("up");
    case NavigationDirection::Down:
        return QStringLiteral("down");
    }
    return QString();
}
} // anonymous namespace

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

    // Initialize shader registry singleton (must be done early, before D-Bus adaptors)
    // The registry checks for Qt6::ShaderTools availability at compile time
    // and for qsb tool availability at runtime
    new ShaderRegistry(this);

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

    // Initialize domain-specific D-Bus adaptors
    // Each adaptor has a single responsibility and its own D-Bus interface
    // D-Bus adaptors use raw new; Qt parent-child manages their lifetime.
    m_layoutAdaptor = new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), this);
    m_layoutAdaptor->setActivityManager(m_activityManager.get());
    m_settingsAdaptor = new SettingsAdaptor(m_settings.get(), this);

    // Overlay adaptor - overlay visibility and highlighting
    m_overlayAdaptor =
        new OverlayAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(), m_settings.get(), this);

    // Zone detection adaptor - zone detection queries
    m_zoneDetectionAdaptor =
        new ZoneDetectionAdaptor(m_zoneDetector.get(), m_layoutManager.get(), m_settings.get(), this);

    // Window tracking adaptor - window-zone assignments
    m_windowTrackingAdaptor = new WindowTrackingAdaptor(m_layoutManager.get(), m_zoneDetector.get(), m_settings.get(),
                                                        m_virtualDesktopManager.get(), this);

    m_screenAdaptor = new ScreenAdaptor(this);

    // Window drag adaptor - handles drag events from KWin script
    // All drag logic (modifiers, zones, snapping) handled here
    m_windowDragAdaptor = new WindowDragAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                                m_settings.get(), m_windowTrackingAdaptor, this);

    // Zone selector methods are called directly from WindowDragAdaptor; QDBusAbstractAdaptor
    // signals are for D-Bus, not Qt connections.

    // Window tracking service - business logic for zone assignments
    m_windowTrackingService = std::make_unique<WindowTrackingService>(
        m_layoutManager.get(), m_zoneDetector.get(), m_settings.get(),
        m_virtualDesktopManager.get(), this);

    // Autotiling engine - automatic window tiling
    m_autotileEngine = std::make_unique<AutotileEngine>(
        m_layoutManager.get(), m_windowTrackingService.get(),
        m_screenManager.get(), this);

    // Autotile adaptor - D-Bus interface for autotiling control
    m_autotileAdaptor = new AutotileAdaptor(m_autotileEngine.get(), this);

    // Overlay needs autotile state to show correct active layout in zone selector
    m_overlayService->setAutotileEngine(m_autotileEngine.get());

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

    // Initialize and start screen manager
    m_screenManager->init();
    m_screenManager->start();

    // Initialize and start virtual desktop manager
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

    // Initialize and start activity manager
    // Connect to VirtualDesktopManager for desktop+activity coordinate lookup
    m_activityManager->setVirtualDesktopManager(m_virtualDesktopManager.get());
    m_activityManager->init();
    if (ActivityManager::isAvailable()) {
        m_activityManager->start();

        // Set initial activity on overlay for per-activity layout resolution
        m_overlayService->setCurrentActivity(m_activityManager->currentActivity());

        // Connect activity changes: update overlay's activity and refresh layout resolution
        connect(m_activityManager.get(), &ActivityManager::currentActivityChanged, this,
                [this](const QString& activityId) {
                    m_overlayService->setCurrentActivity(activityId);
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

    // Register global shortcuts via ShortcutManager
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
    // Quick layout shortcuts (Meta+1-9) - now includes autotile algorithms
    connect(m_shortcutManager.get(), &ShortcutManager::quickLayoutRequested, this, [this](int number) {
        if (m_unifiedLayoutController) {
            m_unifiedLayoutController->applyLayoutByNumber(number);
        }
    });

    // Cycle layout shortcuts (Meta+[/]) - now cycles through manual layouts AND autotile algorithms
    connect(m_shortcutManager.get(), &ShortcutManager::previousLayoutRequested, this, [this]() {
        if (m_unifiedLayoutController) {
            m_unifiedLayoutController->cyclePrevious();
        }
    });
    connect(m_shortcutManager.get(), &ShortcutManager::nextLayoutRequested, this, [this]() {
        if (m_unifiedLayoutController) {
            m_unifiedLayoutController->cycleNext();
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Phase 1 Keyboard Navigation Shortcuts
    // ═══════════════════════════════════════════════════════════════════════════

    // Move window to adjacent zone shortcuts
    connect(m_shortcutManager.get(), &ShortcutManager::moveWindowRequested, this,
            [this](NavigationDirection direction) {
                QString dirStr = navigationDirectionToString(direction);
                if (dirStr.isEmpty()) {
                    qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
                    return;
                }
                m_windowTrackingAdaptor->moveWindowToAdjacentZone(dirStr);
            });

    // Focus navigation to adjacent zone shortcuts
    connect(m_shortcutManager.get(), &ShortcutManager::focusZoneRequested, this, [this](NavigationDirection direction) {
        QString dirStr = navigationDirectionToString(direction);
        if (dirStr.isEmpty()) {
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

    // Toggle window float shortcut (context-aware via router)
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() {
        // Context-aware routing: autotile mode uses engine toggle, manual mode uses zone unsnap
        if (m_shortcutRouter) {
            m_shortcutRouter->toggleFloat();
        } else {
            m_windowTrackingAdaptor->toggleWindowFloat();
        }
    });

    // Swap window with adjacent zone shortcuts
    connect(m_shortcutManager.get(), &ShortcutManager::swapWindowRequested, this,
            [this](NavigationDirection direction) {
                QString dirStr = navigationDirectionToString(direction);
                if (dirStr.isEmpty()) {
                    qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
                    return;
                }
                m_windowTrackingAdaptor->swapWindowWithAdjacentZone(dirStr);
            });

    // Rotate windows in layout shortcuts (context-aware via router)
    connect(m_shortcutManager.get(), &ShortcutManager::rotateWindowsRequested, this, [this](bool clockwise) {
        // Context-aware routing: autotile mode rotates window order, manual mode rotates through zones
        if (m_shortcutRouter) {
            m_shortcutRouter->rotateWindows(clockwise);
        } else {
            m_windowTrackingAdaptor->rotateWindowsInLayout(clockwise);
        }
    });

    // Snap to zone by number shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::snapToZoneRequested, this, [this](int zoneNumber) {
        m_windowTrackingAdaptor->snapToZoneByNumber(zoneNumber);
    });

    // Cycle windows within zone shortcut (context-aware via router)
    connect(m_shortcutManager.get(), &ShortcutManager::cycleWindowsInZoneRequested, this, [this](bool forward) {
        // Context-aware routing: autotile mode uses focusNext/Previous, manual mode cycles in zone
        if (m_shortcutRouter) {
            m_shortcutRouter->cycleWindows(forward);
        } else {
            m_windowTrackingAdaptor->cycleWindowsInZone(forward);
        }
    });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Phase 3.2: Autotile settings initialization
    // ═══════════════════════════════════════════════════════════════════════════════

    // Initialize autotile engine from settings and connect for live updates
    // The engine internally handles debouncing for rapid settings changes
    if (m_autotileEngine && m_settings) {
        m_autotileEngine->syncFromSettings(m_settings.get());
        m_autotileEngine->connectToSettings(m_settings.get());
        qCInfo(lcDaemon) << "Autotile engine connected to settings";
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Mode Tracking & Smart Toggle (Unified Layout Model)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Initialize mode tracker for smart toggle between manual and autotile modes
    m_modeTracker = std::make_unique<ModeTracker>(m_settings.get(), this);
    m_modeTracker->load();

    // Initialize context-aware shortcut router
    m_shortcutRouter = std::make_unique<ContextAwareShortcutRouter>(
        m_modeTracker.get(), m_autotileEngine.get(), m_windowTrackingAdaptor, this);

    // Initialize unified layout controller
    m_unifiedLayoutController = std::make_unique<UnifiedLayoutController>(
        m_layoutManager.get(), m_autotileEngine.get(), m_settings.get(), this);

    // Connect unified layout controller signals for OSD display
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this, [this](Layout* layout) {
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            showLayoutOsd(layout);
        }
    });
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::autotileApplied, this, [this](const QString& algorithmId) {
        showAutotileOsd(algorithmId);
    });

    // Connect layout manager changes to mode tracker for recording last manual layout
    connect(m_layoutManager.get(), &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
        if (layout && m_modeTracker) {
            m_modeTracker->recordManualLayout(layout->id());
        }
    });

    // Connect autotile algorithm changes to mode tracker
    if (m_autotileEngine) {
        connect(m_autotileEngine.get(), &AutotileEngine::algorithmChanged, this, [this](const QString& algorithmId) {
            if (m_modeTracker) {
                m_modeTracker->recordAutotileAlgorithm(algorithmId);
            }
        });

        // Track when autotile is enabled/disabled
        connect(m_autotileEngine.get(), &AutotileEngine::enabledChanged, this, [this](bool enabled) {
            if (m_modeTracker) {
                if (enabled) {
                    // Record autotile mode when enabled
                    m_modeTracker->recordAutotileAlgorithm(m_autotileEngine->algorithm());
                } else {
                    // When disabled, switch to manual mode (keep last layout)
                    m_modeTracker->setCurrentMode(ModeTracker::TilingMode::Manual);
                }
            }
        });
    }

    // Connect zone selector autotile selection (click)
    connect(m_overlayService.get(), &OverlayService::autotileLayoutSelected, this, [this](const QString& algorithmId) {
        if (m_autotileEngine) {
            m_autotileEngine->setAlgorithm(algorithmId);
            m_autotileEngine->setEnabled(true);
            qCInfo(lcDaemon) << "Autotile layout selected from zone selector:" << algorithmId;
            showAutotileOsd(algorithmId);
            // Update mode tracker
            if (m_modeTracker) {
                m_modeTracker->recordAutotileAlgorithm(algorithmId);
            }
        }
    });

    // Connect zone selector manual layout selection (drop on zone)
    connect(m_overlayService.get(), &OverlayService::manualLayoutSelected, this, [this](const QString& layoutId) {
        if (!m_layoutManager) {
            return;
        }
        Layout* layout = m_layoutManager->layoutById(QUuid::fromString(layoutId));
        if (layout) {
            // Disable autotile when switching to manual layout
            if (m_autotileEngine && m_autotileEngine->isEnabled()) {
                m_autotileEngine->setEnabled(false);
                qCInfo(lcDaemon) << "Disabling autotile - switching to manual layout";
            }
            m_layoutManager->setActiveLayout(layout);
            qCInfo(lcDaemon) << "Manual layout selected from zone selector:" << layout->name();
            m_overlayService->showLayoutOsd(layout);
            // Update mode tracker
            if (m_modeTracker) {
                m_modeTracker->recordManualLayout(layout->id());
            }
        }
    });

    // Connect zone selector autotile drop (drag window onto autotile layout)
    connect(m_windowDragAdaptor, &WindowDragAdaptor::autotileDropRequested, this,
            [this](const QString& windowId, const QString& algorithmId) {
                if (m_autotileEngine) {
                    m_autotileEngine->setAlgorithm(algorithmId);
                    m_autotileEngine->setEnabled(true);
                    m_autotileEngine->retile(QString());
                    qCInfo(lcDaemon) << "Window dropped on autotile layout:" << algorithmId
                                    << "- enabling and retiling for window" << windowId;
                    showAutotileOsd(algorithmId);
                }
            });

    // ═══════════════════════════════════════════════════════════════════════════════
    // Phase 2.1: Connect window events to AutotileEngine
    // ═══════════════════════════════════════════════════════════════════════════════
    // WindowTrackingAdaptor receives window events from KWin via D-Bus and emits signals.
    // Connect these to AutotileEngine so it knows about windows for tiling.
    if (m_autotileEngine) {
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowAddedEvent,
                m_autotileEngine.get(), &AutotileEngine::windowOpened);
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowRemovedEvent,
                m_autotileEngine.get(), &AutotileEngine::windowClosed);
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowActivatedEvent,
                m_autotileEngine.get(), &AutotileEngine::windowFocused);
        qCDebug(lcDaemon) << "Connected window tracking events to AutotileEngine";
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Phase 3.1: Autotile shortcut connections (Smart Toggle)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Smart toggle (Meta+T): Toggle between last manual layout and last autotile algorithm
    connect(m_shortcutManager.get(), &ShortcutManager::toggleAutotileRequested, this, [this]() {
        if (!m_modeTracker || !m_autotileEngine) {
            return;
        }

        auto oldMode = m_modeTracker->currentMode();
        auto newMode = m_modeTracker->toggleMode();

        // Check if toggle actually succeeded (may fail if no layout recorded)
        if (newMode == oldMode) {
            qCDebug(lcDaemon) << "Smart toggle: mode unchanged (toggle may have been blocked)";
            m_overlayService->showNavigationOsd(false, QStringLiteral("toggle"),
                i18n("No layout recorded - cannot switch modes"));
            return;
        }

        if (newMode == ModeTracker::TilingMode::Autotile) {
            // Switching TO autotile mode
            QString algorithmId = m_modeTracker->lastAutotileAlgorithm();
            m_autotileEngine->setAlgorithm(algorithmId);
            m_autotileEngine->setEnabled(true);
            qCInfo(lcDaemon) << "Smart toggle: switched to Autotile mode -" << algorithmId;
            showAutotileOsd(algorithmId);
        } else {
            // Switching TO manual mode
            m_autotileEngine->setEnabled(false);

            // Restore last manual layout if available
            QString layoutId = m_modeTracker->lastManualLayoutId();
            if (!layoutId.isEmpty()) {
                auto uuidOpt = Utils::parseUuid(layoutId);
                if (uuidOpt) {
                    Layout* layout = m_layoutManager->layoutById(*uuidOpt);
                    if (layout) {
                        m_layoutManager->setActiveLayout(layout);
                        qCInfo(lcDaemon) << "Smart toggle: switched to Manual mode -" << layout->name();

                        // Show OSD with layout name
                        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
                            showLayoutOsd(layout);
                        }
                    }
                }
            } else {
                qCInfo(lcDaemon) << "Smart toggle: switched to Manual mode (no previous layout)";
                if (m_settings && m_settings->showNavigationOsd()) {
                    m_overlayService->showNavigationOsd(true, QStringLiteral("mode"), QStringLiteral("Manual"));
                }
            }
        }
    });

    // Focus master window
    connect(m_shortcutManager.get(), &ShortcutManager::focusMasterRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->focusMaster();
        }
    });

    // Swap focused window with master
    connect(m_shortcutManager.get(), &ShortcutManager::swapMasterRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->swapFocusedWithMaster();
        }
    });

    // Increase master area ratio
    connect(m_shortcutManager.get(), &ShortcutManager::incMasterRatioRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->increaseMasterRatio();
        }
    });

    // Decrease master area ratio
    connect(m_shortcutManager.get(), &ShortcutManager::decMasterRatioRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->decreaseMasterRatio();
        }
    });

    // Increase master window count
    connect(m_shortcutManager.get(), &ShortcutManager::incMasterCountRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->increaseMasterCount();
        }
    });

    // Decrease master window count
    connect(m_shortcutManager.get(), &ShortcutManager::decMasterCountRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->decreaseMasterCount();
        }
    });

    // Force retile all windows
    connect(m_shortcutManager.get(), &ShortcutManager::retileRequested, this, [this]() {
        if (m_autotileEngine) {
            m_autotileEngine->retile();
            qCInfo(lcDaemon) << "Manual retile triggered";
        }
    });

    // Connect navigation feedback signal to show OSD (Qt signal from WindowTrackingAdaptor)
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
            [this](bool success, const QString& action, const QString& reason) {
                // Only show OSD if setting is enabled
                if (m_settings && m_settings->showNavigationOsd()) {
                    m_overlayService->showNavigationOsd(success, action, reason);
                }
            });

    // Note: KWin effect reports navigation feedback via reportNavigationFeedback D-Bus method,
    // which emits the Qt navigationFeedback signal. No D-Bus signal connection needed.

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

    // Save mode tracker state (ensures last mode/layout survives shutdown)
    if (m_modeTracker) {
        m_modeTracker->save();
    }

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

void Daemon::showLayoutOsd(Layout* layout)
{
    if (!layout) {
        return;
    }

    const QString layoutName = layout->name();

    // Check OSD style setting
    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;

    switch (style) {
    case OsdStyle::None:
        // No OSD
        qCDebug(lcDaemon) << "OSD disabled, skipping for layout:" << layoutName;
        return;

    case OsdStyle::Text:
        // Use KDE Plasma's OSD service for text-only notification
        {
            QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));

            QString displayText = i18n("Zone Layout: %1", layoutName);
            msg << QStringLiteral("plasmazones") << displayText;

            QDBusConnection::sessionBus().asyncCall(msg);
            qCDebug(lcDaemon) << "Showing text OSD for layout:" << layoutName;
        }
        break;

    case OsdStyle::Preview:
        // Use visual layout preview OSD
        if (m_overlayService) {
            m_overlayService->showLayoutOsd(layout);
            qCDebug(lcDaemon) << "Showing preview OSD for layout:" << layoutName;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

void Daemon::showAutotileOsd(const QString& algorithmId)
{
    if (!m_settings || !m_settings->showOsdOnLayoutSwitch()) {
        return;
    }

    auto* registry = AlgorithmRegistry::instance();
    if (!registry) {
        return;
    }

    TilingAlgorithm* algo = registry->algorithm(algorithmId);
    if (!algo) {
        return;
    }

    // Use shared utility to generate preview zones
    QVariantList zonesList = AlgorithmRegistry::generatePreviewZones(algo);

    // Show visual OSD with autotile preview (category=1 for Autotile)
    QString layoutId = LayoutId::makeAutotileId(algorithmId);
    m_overlayService->showLayoutOsd(layoutId, algo->name(), zonesList, 1);
}

// Unified layout management now handled by UnifiedLayoutController
// Screen management now handled by ScreenManager
// Shortcut management now handled by ShortcutManager
// Signals are connected in start() method
// Note: Navigation feedback from KWin effect comes via reportNavigationFeedback D-Bus method,
// which emits the Qt navigationFeedback signal handled by the connection above.

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

    // Use current desktop and activity so per-desktop/per-activity assignments are respected
    const int currentDesktop = m_virtualDesktopManager->currentDesktop();
    const QString currentActivity = m_activityManager && ActivityManager::isAvailable()
        ? m_activityManager->currentActivity()
        : QString();

    // Choose geometry for active layout recalc: prefer primary screen when in batch, else first
    const QString primaryName = [this]() {
        QScreen* p = Utils::primaryScreen();
        return p ? p->name() : QString();
    }();
    QRect activeLayoutGeometry;
    if (!primaryName.isEmpty() && m_pendingGeometryUpdates.contains(primaryName)) {
        activeLayoutGeometry = m_pendingGeometryUpdates.value(primaryName);
    } else {
        activeLayoutGeometry = m_pendingGeometryUpdates.constBegin().value();
    }

    // Process all pending geometry updates in a single batch
    // This prevents N×M work when multiple screens change simultaneously
    bool activeLayoutRecalcDone = false;
    for (auto it = m_pendingGeometryUpdates.constBegin(); it != m_pendingGeometryUpdates.constEnd(); ++it) {
        const QString& screenName = it.key();
        const QRect& availableGeometry = it.value();

        qCDebug(lcDaemon) << "Processing geometry update for" << screenName
                          << "availableGeometry:" << availableGeometry;

        // Recalculate zone geometries for active layout at most once (primary screen, or first).
        // Active layout is global; recalc'ing per-screen overwrites each time (last-wins bug).
        if (m_layoutManager->activeLayout() && !activeLayoutRecalcDone) {
            m_layoutManager->activeLayout()->recalculateZoneGeometries(activeLayoutGeometry);
            activeLayoutRecalcDone = true;
        }

        // Update screen-specific layout if different from active
        if (Layout* screenLayout =
                m_layoutManager->layoutForScreen(screenName, currentDesktop, currentActivity)) {
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
