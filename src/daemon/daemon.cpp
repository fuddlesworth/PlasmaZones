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
#include <QDBusPendingCall>
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
#include "../core/geometryutils.h"
#include "../core/logging.h"
#include "../core/utils.h"
#include "../core/windowtrackingservice.h"
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
// Geometry/panel timing (ms) — keep in sync with comments in processPendingGeometryUpdates
// Debounce: coalesce rapid geometry changes (multi-screen, panel editor) into one update.
constexpr int GEOMETRY_UPDATE_DEBOUNCE_MS = 400;
// After processing geometry we re-query panels once so we pick up settled state (e.g. panel editor close).
constexpr int DELAYED_PANEL_REQUERY_MS = 400;
// Reapply requested on next event loop (0); daemon state is already updated when we start the timer.
constexpr int REAPPLY_DELAY_MS = 0;

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
    , m_settings(std::make_unique<Settings>(nullptr))
    , m_zoneDetector(std::make_unique<ZoneDetector>(m_settings.get(), nullptr))
    , m_overlayService(std::make_unique<OverlayService>(nullptr))
    , m_screenManager(std::make_unique<ScreenManager>(nullptr))
    , m_virtualDesktopManager(std::make_unique<VirtualDesktopManager>(m_layoutManager.get(), nullptr))
    , m_activityManager(std::make_unique<ActivityManager>(m_layoutManager.get(), nullptr))
    , m_shortcutManager(std::make_unique<ShortcutManager>(m_settings.get(), m_layoutManager.get(), nullptr))
{
    // Configure geometry update debounce timer
    // This prevents cascading recalculations when multiple geometry changes occur rapidly.
    // Use a longer debounce so KDE panel edit mode exit and other transient
    // changes settle before we recalculate zones and overlay.
    m_geometryUpdateTimer.setSingleShot(true);
    m_geometryUpdateTimer.setInterval(GEOMETRY_UPDATE_DEBOUNCE_MS);
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

    // Recalculate zone geometries for ALL layouts so that fixed-mode zones
    // have correct normalized coordinates for preview rendering (KCM, OSD, selector).
    if (QScreen* primary = Utils::primaryScreen()) {
        for (Layout* layout : m_layoutManager->layouts()) {
            layout->recalculateZoneGeometries(
                GeometryUtils::effectiveScreenGeometry(layout, primary));
        }
    }

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
                    GeometryUtils::effectiveScreenGeometry(layout, primary));
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
                    layout->recalculateZoneGeometries(
                        GeometryUtils::effectiveScreenGeometry(layout, screen));
                }
                // Note: We don't change zone detector or overlay here since
                // they work with the active layout, not per-screen layouts
            });

    // Connect settings changes to overlay service
    connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());
    });

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

    // Reapply window geometries after each geometry batch (processPendingGeometryUpdates).
    // When the delayed panel requery completes it emits availableGeometryChanged, which triggers
    // the same debounce → processPendingGeometryUpdates → reapply path; no separate delay needed.
    m_reapplyGeometriesTimer.setSingleShot(true);
    connect(&m_reapplyGeometriesTimer, &QTimer::timeout, m_windowTrackingAdaptor,
            &WindowTrackingAdaptor::requestReapplyWindowGeometries);

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

    // Warn about identical monitors producing duplicate screen IDs
    Utils::warnDuplicateScreenIds();

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
        // Invalidate cached EDID serial so a fresh sysfs read happens for this connector
        // (handles the case where EDID wasn't available during very early startup)
        Utils::invalidateEdidCache(screen->name());
        m_overlayService->handleScreenAdded(screen);
        // Use per-screen layout (falls back to activeLayout if no assignment)
        Layout* screenLayout = m_layoutManager->layoutForScreen(
            Utils::screenIdentifier(screen), m_virtualDesktopManager->currentDesktop(),
            m_activityManager && ActivityManager::isAvailable()
                ? m_activityManager->currentActivity() : QString());
        if (screenLayout) {
            screenLayout->recalculateZoneGeometries(
                GeometryUtils::effectiveScreenGeometry(screenLayout, screen));
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
            if (allowed.isEmpty()) continue;
            bool changed = false;
            changed |= (allowed.removeAll(removedScreenId) > 0);
            changed |= (allowed.removeAll(removedName) > 0);
            if (changed) {
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
            m_unifiedLayoutController->setCurrentScreenName(Utils::screenIdentifier(screen));
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
            m_unifiedLayoutController->setCurrentScreenName(Utils::screenIdentifier(screen));
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
            m_unifiedLayoutController->setCurrentScreenName(Utils::screenIdentifier(screen));
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

    // Layout picker shortcut (interactive layout browser + resnap)
    // Capture screen name at open time so it's still valid after the picker closes.
    connect(m_shortcutManager.get(), &ShortcutManager::layoutPickerRequested, this, [this]() {
        if (!m_unifiedLayoutController) {
            return;
        }
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        if (!screen) {
            qCDebug(lcDaemon) << "No screen info for layoutPicker shortcut — skipping";
            return;
        }
        const QString screenName = Utils::screenIdentifier(screen);
        m_unifiedLayoutController->setCurrentScreenName(screenName);
        m_overlayService->showLayoutPicker(screenName);
    });
    connect(m_overlayService.get(), &OverlayService::layoutPickerSelected, this, [this](const QString& layoutId) {
        if (!m_unifiedLayoutController) {
            return;
        }
        // Screen name was already set when the picker opened.
        m_unifiedLayoutController->applyLayoutById(layoutId);
        // Suppress resnap OSD — the layout switch OSD already provides feedback
        m_suppressResnapOsd = true;
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
            QString screenId = Utils::screenIdForName(screenName);
            m_layoutManager->assignLayout(screenId, m_virtualDesktopManager->currentDesktop(),
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

    // Connect Snap Assist selection: fetch authoritative zone geometry from service (same as
    // keyboard navigation) to avoid overlay coordinate drift/overlap bugs, then forward to effect
    connect(m_overlayService.get(), &IOverlayService::snapAssistWindowSelected, this,
            [this](const QString& windowId, const QString& zoneId, const QString& geometryJson,
                   const QString& screenName) {
                QString geometryToUse = geometryJson;
                QString effectiveScreen = screenName;
                if (effectiveScreen.isEmpty() && QGuiApplication::primaryScreen()) {
                    effectiveScreen = QGuiApplication::primaryScreen()->name();
                }
                if (!effectiveScreen.isEmpty()) {
                    QString authGeometry =
                        m_windowTrackingAdaptor->getZoneGeometryForScreen(zoneId, effectiveScreen);
                    if (!authGeometry.isEmpty()) {
                        geometryToUse = authGeometry;
                    }
                }
                m_windowTrackingAdaptor->requestMoveSpecificWindowToZone(windowId, zoneId, geometryToUse);
            });

    // Connect navigation feedback signal to show OSD (Qt signal from WindowTrackingAdaptor)
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
            [this](bool success, const QString& action, const QString& reason,
                   const QString& sourceZoneId, const QString& targetZoneId, const QString& screenName) {
                // Suppress resnap OSD when triggered from layout picker (layout switch OSD is sufficient)
                if (m_suppressResnapOsd && action == QStringLiteral("resnap")) {
                    m_suppressResnapOsd = false;
                    return;
                }
                // Only show OSD if setting is enabled
                if (m_settings && m_settings->showNavigationOsd()) {
                    m_overlayService->showNavigationOsd(success, action, reason, sourceZoneId, targetZoneId, screenName);
                }
            });

    // Note: KWin effect reports navigation feedback via reportNavigationFeedback D-Bus method,
    // which emits the Qt navigationFeedback signal. No D-Bus signal connection needed.

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

    m_running = true;

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
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->saveStateOnShutdown();
    }

    // Save mode tracker state (ensures last mode/layout survives shutdown)
    if (m_modeTracker) {
        m_modeTracker->save();
    }

    m_reapplyGeometriesTimer.stop();
    m_running = false;
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

    // Recalculate zone geometries for ALL layouts so fixed-mode zones stay
    // normalized correctly.  Uses primary screen as the reference geometry
    // (per-layout useFullScreenGeometry is respected by effectiveScreenGeometry).
    QScreen* primaryScreen = Utils::primaryScreen();
    if (primaryScreen) {
        for (Layout* layout : m_layoutManager->layouts()) {
            layout->recalculateZoneGeometries(
                GeometryUtils::effectiveScreenGeometry(layout, primaryScreen));
        }
    }

    m_pendingGeometryUpdates.clear();

    // Single overlay update after all geometry recalculations
    m_overlayService->updateGeometries();

    // Ask effect to reapply snapped window positions (next event loop when REAPPLY_DELAY_MS is 0).
    m_reapplyGeometriesTimer.setInterval(REAPPLY_DELAY_MS);
    m_reapplyGeometriesTimer.start();

    // Re-query panel geometry once after a delay to pick up settled state (e.g. panel editor close).
    // That completion emits availableGeometryChanged → debounce → processPendingGeometryUpdates → reapply.
    m_screenManager->scheduleDelayedPanelRequery(DELAYED_PANEL_REQUERY_MS);
}

} // namespace PlasmaZones
