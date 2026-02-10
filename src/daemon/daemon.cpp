// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include <QScreen>
#include <QCursor>
#include <QAction>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusError>
#include <QFile>
#include <QThread>
#include <QProcess>

#include <KGlobalAccel>
#include <KLocalizedString>
#include <KSharedConfig>
#include <KConfigGroup>

#include "overlayservice.h"
#include "modetracker.h"
#include "unifiedlayoutcontroller.h"
#include "zoneselectorcontroller.h"
#include "shortcutmanager.h"
#include "rendering/zoneshadernoderhi.h"
#include "../core/layoutmanager.h"
#include "../core/zonedetector.h"
#include "../core/screenmanager.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/constants.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../core/windowtrackingservice.h"
#include "../core/autotileservice.h"
#include "../core/shaderregistry.h"
#include "../config/settings.h"
#include "../dbus/layoutadaptor.h"
#include "../dbus/settingsadaptor.h"
#include "../dbus/overlayadaptor.h"
#include "../dbus/zonedetectionadaptor.h"
#include "../dbus/windowtrackingadaptor.h"
#include "../dbus/screenadaptor.h"
#include "../dbus/windowdragadaptor.h"

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

/**
 * @brief Resolve current screen for keyboard shortcuts
 *
 * Primary source: the cursor's screen, reported by the KWin effect via
 * cursorScreenChanged (fires on every monitor crossing in slotMouseChanged).
 * This accurately reflects where the user is looking, even if no window
 * on that screen has focus.
 *
 * Fallback: the focused window's screen, reported via windowActivated.
 * Used when the effect hasn't loaded yet or no mouse movement has occurred.
 *
 * QCursor::pos() is NOT used — it returns stale data for background daemons
 * on Wayland.
 */
QScreen* resolveShortcutScreen(const WindowTrackingAdaptor* trackingAdaptor)
{
    if (!trackingAdaptor) {
        return nullptr;
    }

    // Prefer cursor screen — tracks the physical cursor position
    const QString& cursorScreen = trackingAdaptor->lastCursorScreenName();
    if (!cursorScreen.isEmpty()) {
        QScreen* screen = Utils::findScreenByName(cursorScreen);
        if (screen) {
            return screen;
        }
    }

    // Cursor screen not yet reported (effect not loaded or no mouse movement).
    // Fall back to focused window's screen.
    const QString& activeScreen = trackingAdaptor->lastActiveScreenName();
    if (!activeScreen.isEmpty()) {
        QScreen* screen = Utils::findScreenByName(activeScreen);
        if (screen) {
            return screen;
        }
    }

    // Last resort: primary screen (daemon just started, no KWin effect data yet)
    qCDebug(lcDaemon) << "resolveShortcutScreen: falling back to primary screen";
    return Utils::primaryScreen();
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
    auto* shaderRegistry = new ShaderRegistry(this);
    auto scheduleWarmForShader = [this, registryPtr = QPointer<ShaderRegistry>(shaderRegistry)](const ShaderRegistry::ShaderInfo& info) {
        if (ShaderRegistry::isNoneShader(info.id) || !info.isValid()) {
            return;
        }
        if (info.vertexShaderPath.isEmpty() || info.sourcePath.isEmpty()) {
            return;
        }
        if (!QFile::exists(info.vertexShaderPath) || !QFile::exists(info.sourcePath)) {
            return;
        }
        ShaderRegistry* reg = registryPtr.data();
        if (!reg) {
            return;
        }
        const QString shaderId = info.id;
        auto* watcher = new QFutureWatcher<WarmShaderBakeResult>(this);
        connect(watcher, &QFutureWatcher<WarmShaderBakeResult>::finished, this, [registryPtr, watcher, shaderId]() {
            if (!registryPtr) {
                watcher->deleteLater();
                return;
            }
            const WarmShaderBakeResult r = watcher->result();
            if (!r.success) {
                qCWarning(lcDaemon) << "Shader bake failed for" << shaderId << ":" << r.errorMessage;
            }
            registryPtr->reportShaderBakeFinished(shaderId, r.success, r.errorMessage);
            watcher->deleteLater();
        });
        reg->reportShaderBakeStarted(shaderId);
        watcher->setFuture(QtConcurrent::run([vertPath = info.vertexShaderPath, fragPath = info.sourcePath]() {
            return warmShaderBakeCacheForPaths(vertPath, fragPath);
        }));
    };
    connect(shaderRegistry, &ShaderRegistry::shadersChanged, this, [scheduleWarmForShader]() {
        const QList<ShaderRegistry::ShaderInfo> shaders = ShaderRegistry::instance()->availableShaders();
        for (const ShaderRegistry::ShaderInfo& info : shaders) {
            scheduleWarmForShader(info);
        }
    });
    // Warm cache once for shaders already loaded by ShaderRegistry ctor
    for (const ShaderRegistry::ShaderInfo& info : shaderRegistry->availableShaders()) {
        scheduleWarmForShader(info);
    }

    m_layoutManager->setSettings(m_settings.get());
    // Load layouts (defaultLayout() reads settings internally)
    m_layoutManager->loadLayouts();
    m_layoutManager->loadAssignments();

    // Configure overlay service with settings, layout manager, and default layout
    m_overlayService->setSettings(m_settings.get());
    m_overlayService->setLayoutManager(m_layoutManager.get());
    if (auto* defLayout = m_layoutManager->defaultLayout()) {
        m_overlayService->setLayout(defLayout);
        m_zoneDetector->setLayout(defLayout);
        qCInfo(lcDaemon) << "Overlay configured layout= " << defLayout->name()
                         << " zones= " << defLayout->zoneCount();
    } else {
        qCWarning(lcDaemon) << "No default layout available for overlay";
    }

    // Connect layout changes to zone detector and overlay service
    // activeLayoutChanged fires when the global active layout changes; layoutAssigned
    // fires for per-screen assignments. We handle both but avoid redundant recalculations.
    connect(m_layoutManager.get(), &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
        if (layout) {
            // Recalculate zone geometries once using primary screen geometry.
            // Active layout is global; recalculating per-screen overwrites each
            // iteration (last-wins bug). The overlay computes per-screen geometry
            // on the fly via GeometryUtils::getZoneGeometryWithGaps().
            QScreen* primary = Utils::primaryScreen();
            if (primary) {
                layout->recalculateZoneGeometries(
                    ScreenManager::actualAvailableGeometry(primary));
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
    // Each adaptor has its own D-Bus interface
    // D-Bus adaptors use raw new; Qt parent-child manages their lifetime.
    m_layoutAdaptor = new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), this);
    m_layoutAdaptor->setActivityManager(m_activityManager.get());
    // Invalidate D-Bus getActiveLayout() cache when the default layout changes in settings
    connect(m_settings.get(), &Settings::defaultLayoutIdChanged, m_layoutAdaptor, &LayoutAdaptor::invalidateCache);
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

    // Auto-tile service - dynamic zone regeneration (#108, #106, #107)
    m_autoTileService = std::make_unique<AutoTileService>(
        m_layoutManager.get(), m_windowTrackingService.get(), m_settings.get(), this);
    m_windowTrackingAdaptor->setAutoTileService(m_autoTileService.get());

    // Connect AutoTileService geometriesChanged to D-Bus signal emission
    connect(m_autoTileService.get(), &AutoTileService::geometriesChanged,
            m_windowTrackingAdaptor, &WindowTrackingAdaptor::emitAutoTileGeometriesChanged);

    // Wire layout changes to auto-tile service for Dynamic layout tracking (#108)
    connect(m_layoutManager.get(), &LayoutManager::activeLayoutChanged, this,
            [this](Layout*) {
                // Global layout change — notify auto-tile for all screens
                const auto screens = QGuiApplication::screens();
                for (QScreen* screen : screens) {
                    m_autoTileService->handleLayoutChanged(screen->name());
                }
            });
    connect(m_layoutManager.get(), &LayoutManager::layoutAssigned, this,
            [this](const QString& screenName, Layout*) {
                m_autoTileService->handleLayoutChanged(screenName);
            });

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
                int delayMs = 1000 * (attempt + 1); // Linear backoff: 1s, 2s, 3s
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

    qCInfo(lcDaemon) << "D-Bus service registered service= " << DBus::ServiceName << " path= " << DBus::ObjectPath;

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
        // Update all components with current desktop for per-desktop layout lookup
        // NOTE: LayoutManager is the single source of truth for desktop/activity.
        // WindowDragAdaptor reads from LayoutManager directly via resolveLayoutForScreen().
        m_overlayService->setCurrentVirtualDesktop(desktop);
        m_layoutManager->setCurrentVirtualDesktop(desktop);
        if (m_unifiedLayoutController) {
            m_unifiedLayoutController->setCurrentVirtualDesktop(desktop);
        }
        if (m_overlayService->isVisible()) {
            m_overlayService->updateGeometries();
        }
    });

    // Set initial virtual desktop on components that maintain their own copy
    // (WindowDragAdaptor reads from LayoutManager directly via resolveLayoutForScreen())
    const int initialDesktop = m_virtualDesktopManager->currentDesktop();
    m_overlayService->setCurrentVirtualDesktop(initialDesktop);

    // Initialize and start activity manager
    // Connect to VirtualDesktopManager for desktop+activity coordinate lookup
    m_activityManager->setVirtualDesktopManager(m_virtualDesktopManager.get());
    m_activityManager->init();
    if (ActivityManager::isAvailable()) {
        m_activityManager->start();

        // Set initial activity on components that maintain their own copy
        m_overlayService->setCurrentActivity(m_activityManager->currentActivity());

        // Connect activity changes: update all components
        connect(m_activityManager.get(), &ActivityManager::currentActivityChanged, this,
                [this](const QString& activityId) {
                    m_overlayService->setCurrentActivity(activityId);
                    m_layoutManager->setCurrentActivity(activityId);
                    if (m_unifiedLayoutController) {
                        m_unifiedLayoutController->setCurrentActivity(activityId);
                    }
                    if (m_overlayService->isVisible()) {
                        m_overlayService->updateGeometries();
                    }
                });
    }

    // Connect screen manager signals
    connect(m_screenManager.get(), &ScreenManager::screenAdded, this, [this](QScreen* screen) {
        m_overlayService->handleScreenAdded(screen);
        // Use per-screen layout (falls back to activeLayout if no assignment)
        Layout* screenLayout = m_layoutManager->layoutForScreen(
            screen->name(), m_virtualDesktopManager->currentDesktop(),
            m_activityManager && ActivityManager::isAvailable()
                ? m_activityManager->currentActivity() : QString());
        if (screenLayout) {
            screenLayout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));
        }
    });

    connect(m_screenManager.get(), &ScreenManager::screenRemoved, this, [this](QScreen* screen) {
        m_overlayService->handleScreenRemoved(screen);

        // Clean stale screen name from layout visibility restrictions
        const QString removedName = screen->name();
        for (Layout* layout : m_layoutManager->layouts()) {
            QStringList allowed = layout->allowedScreens();
            if (allowed.isEmpty()) continue;
            if (allowed.removeAll(removedName) > 0) {
                layout->setAllowedScreens(allowed);
            }
        }
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
    // Screen detection: On X11, QCursor::pos() works; on Wayland, background daemons
    // get stale cursor data. resolveShortcutScreen() handles both by falling back to
    // the screen reported by the KWin effect's windowActivated D-Bus call.
    connect(m_shortcutManager.get(), &ShortcutManager::openEditorRequested, this, [this]() {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen && m_unifiedLayoutController && !m_unifiedLayoutController->currentScreenName().isEmpty()) {
            screen = Utils::findScreenByName(m_unifiedLayoutController->currentScreenName());
        }
        if (screen) {
            m_layoutAdaptor->openEditorForScreen(screen->name());
        } else {
            m_layoutAdaptor->openEditor();
        }
    });
    // Quick layout shortcuts (Meta+1-9)
    connect(m_shortcutManager.get(), &ShortcutManager::quickLayoutRequested, this, [this](int number) {
        if (!m_unifiedLayoutController) {
            return;
        }
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (screen) {
            m_unifiedLayoutController->setCurrentScreenName(screen->name());
        } else {
            qCDebug(lcDaemon) << "No screen info for quickLayout shortcut — skipping";
            return;
        }
        m_unifiedLayoutController->applyLayoutByNumber(number);
    });

    // Cycle layout shortcuts (Meta+[/])
    connect(m_shortcutManager.get(), &ShortcutManager::previousLayoutRequested, this, [this]() {
        if (!m_unifiedLayoutController) {
            return;
        }
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (screen) {
            m_unifiedLayoutController->setCurrentScreenName(screen->name());
        } else {
            qCDebug(lcDaemon) << "No screen info for previousLayout shortcut — skipping";
            return;
        }
        m_unifiedLayoutController->cyclePrevious();
    });
    connect(m_shortcutManager.get(), &ShortcutManager::nextLayoutRequested, this, [this]() {
        if (!m_unifiedLayoutController) {
            return;
        }
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (screen) {
            m_unifiedLayoutController->setCurrentScreenName(screen->name());
        } else {
            qCDebug(lcDaemon) << "No screen info for nextLayout shortcut — skipping";
            return;
        }
        m_unifiedLayoutController->cycleNext();
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Keyboard Navigation Shortcuts
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
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for pushToEmptyZone shortcut — skipping";
            return;
        }
        m_windowTrackingAdaptor->pushToEmptyZone(screen->name());
    });

    // Restore window size shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::restoreWindowSizeRequested, this, [this]() {
        m_windowTrackingAdaptor->restoreWindowSize();
    });

    // Toggle window float shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() {
        m_windowTrackingAdaptor->toggleWindowFloat();
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

    // Rotate windows in layout shortcuts
    connect(m_shortcutManager.get(), &ShortcutManager::rotateWindowsRequested, this, [this](bool clockwise) {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for rotateWindows shortcut — skipping";
            return;
        }
        m_windowTrackingAdaptor->rotateWindowsInLayout(clockwise, screen->name());
    });

    // Snap to zone by number shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::snapToZoneRequested, this, [this](int zoneNumber) {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for snapToZone shortcut — skipping";
            return;
        }
        m_windowTrackingAdaptor->snapToZoneByNumber(zoneNumber, screen->name());
    });

    // Cycle windows within zone shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::cycleWindowsInZoneRequested, this, [this](bool forward) {
        m_windowTrackingAdaptor->cycleWindowsInZone(forward);
    });

    // Resnap to new layout shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::resnapToNewLayoutRequested, this, [this]() {
        m_windowTrackingAdaptor->resnapToNewLayout();
    });

    // Snap all windows shortcut
    connect(m_shortcutManager.get(), &ShortcutManager::snapAllWindowsRequested, this, [this]() {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for snapAllWindows shortcut — skipping";
            return;
        }
        m_windowTrackingAdaptor->snapAllWindows(screen->name());
    });

    // ═══════════════════════════════════════════════════════════════════════════
    // Auto-Tiling Shortcuts (#106, #107)
    // ═══════════════════════════════════════════════════════════════════════════

    // Promote to master (Meta+Return) - #106
    connect(m_shortcutManager.get(), &ShortcutManager::promoteToMasterRequested, this, [this]() {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for promoteMaster shortcut — skipping";
            return;
        }
        m_windowTrackingAdaptor->promoteMasterWindow(screen->name());
    });

    // Increase master ratio (Meta+L) - #107
    connect(m_shortcutManager.get(), &ShortcutManager::increaseMasterRatioRequested, this, [this]() {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for increaseMasterRatio shortcut — skipping";
            return;
        }
        qreal step = m_settings ? m_settings->masterRatioStep() : 0.05;
        m_windowTrackingAdaptor->adjustMasterRatio(screen->name(), step);
    });

    // Decrease master ratio (Meta+H) - #107
    connect(m_shortcutManager.get(), &ShortcutManager::decreaseMasterRatioRequested, this, [this]() {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for decreaseMasterRatio shortcut — skipping";
            return;
        }
        qreal step = m_settings ? m_settings->masterRatioStep() : 0.05;
        m_windowTrackingAdaptor->adjustMasterRatio(screen->name(), -step);
    });

    // Initialize mode tracker for last-used layout
    m_modeTracker = std::make_unique<ModeTracker>(m_settings.get(), this);
    m_modeTracker->load();

    // Initialize unified layout controller (manual layouts only)
    m_unifiedLayoutController = std::make_unique<UnifiedLayoutController>(
        m_layoutManager.get(), m_settings.get(), this);

    // Set initial desktop/activity context for visibility-filtered cycling
    m_layoutManager->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());
    m_unifiedLayoutController->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());
    if (m_activityManager && ActivityManager::isAvailable()) {
        m_layoutManager->setCurrentActivity(m_activityManager->currentActivity());
        m_unifiedLayoutController->setCurrentActivity(m_activityManager->currentActivity());
    }

    // Connect unified layout controller signals for OSD display
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this, [this](Layout* layout) {
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            showLayoutOsd(layout, m_unifiedLayoutController->currentScreenName());
        }
    });

    // Connect layout manager changes to mode tracker for recording last manual layout
    connect(m_layoutManager.get(), &LayoutManager::activeLayoutChanged, this, [this](Layout* layout) {
        if (layout && m_modeTracker) {
            m_modeTracker->recordManualLayout(layout->id());
        }
    });

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
            m_layoutManager->assignLayout(screenName, m_virtualDesktopManager->currentDesktop(),
                m_activityManager && ActivityManager::isAvailable()
                    ? m_activityManager->currentActivity() : QString(),
                layout);
        }
        // Always update global active layout — fires activeLayoutChanged which
        // populates the resnap buffer, cleans stale assignments, updates OSD, etc.
        m_layoutManager->setActiveLayout(layout);
        qCInfo(lcDaemon) << "Manual layout selected from zone selector:" << layout->name()
                         << "on screen:" << screenName;
        m_overlayService->showLayoutOsd(layout, screenName);
        if (m_modeTracker) {
            m_modeTracker->recordManualLayout(layout->id());
        }
    });

    // Connect navigation feedback signal to show OSD (Qt signal from WindowTrackingAdaptor)
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
            [this](bool success, const QString& action, const QString& reason,
                   const QString& sourceZoneId, const QString& targetZoneId, const QString& screenName) {
                // Only show OSD if setting is enabled
                if (m_settings && m_settings->showNavigationOsd()) {
                    m_overlayService->showNavigationOsd(success, action, reason, sourceZoneId, targetZoneId, screenName);
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

void Daemon::showLayoutOsd(Layout* layout, const QString& screenName)
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
        qCInfo(lcDaemon) << "OSD disabled, skipping for layout:" << layoutName;
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
            qCInfo(lcDaemon) << "Showing text OSD for layout:" << layoutName;
        }
        break;

    case OsdStyle::Preview:
        // Use visual layout preview OSD
        if (m_overlayService) {
            m_overlayService->showLayoutOsd(layout, screenName);
            qCInfo(lcDaemon) << "Showing preview OSD for layout:" << layoutName << "on screen:" << screenName;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
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

        qCInfo(lcDaemon) << "Processing geometry update screen= " << screenName
                         << " availableGeometry= " << availableGeometry;

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
