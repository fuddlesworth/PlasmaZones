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
#include "../dbus/autotileadaptor.h"
#include "../autotile/AutotileEngine.h"
#include "../autotile/AlgorithmRegistry.h"
#include "../autotile/TilingAlgorithm.h"

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

    // Connect settings changes to overlay service and autotile engine
    connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());
        if (m_autotileEngine) {
            m_autotileEngine->syncFromSettings(m_settings.get());
        }
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
    m_windowTrackingAdaptor->setZoneDetectionAdaptor(m_zoneDetectionAdaptor);

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

    // Initialize autotile engine
    m_autotileEngine = std::make_unique<AutotileEngine>(
        m_layoutManager.get(), m_windowTrackingAdaptor->service(), m_screenManager.get(), this);
    m_autotileEngine->syncFromSettings(m_settings.get());
    m_autotileEngine->connectToSettings(m_settings.get());

    // Give the window drag adaptor access to the autotile engine for per-screen
    // autotile checks (overlay suppression and snap rejection on autotile screens)
    m_windowDragAdaptor->setAutotileEngine(m_autotileEngine.get());

    // Create autotile D-Bus adaptor
    m_autotileAdaptor = new AutotileAdaptor(m_autotileEngine.get(), this);

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
        // Per-desktop assignments may differ — recompute autotile screens
        updateAutotileScreens();
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
                    // Per-activity assignments may differ — recompute autotile screens
                    updateAutotileScreens();
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
            screenLayout->recalculateZoneGeometries(ScreenManager::actualAvailableGeometry(screen));
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

    // Navigation shortcuts — single code path per operation (handleXxx)
    connect(m_shortcutManager.get(), &ShortcutManager::moveWindowRequested, this, [this](NavigationDirection d) { handleMove(d); });
    connect(m_shortcutManager.get(), &ShortcutManager::focusZoneRequested, this, [this](NavigationDirection d) { handleFocus(d); });
    connect(m_shortcutManager.get(), &ShortcutManager::pushToEmptyZoneRequested, this, [this]() { handlePush(); });
    connect(m_shortcutManager.get(), &ShortcutManager::restoreWindowSizeRequested, this, [this]() { handleRestore(); });
    connect(m_shortcutManager.get(), &ShortcutManager::toggleWindowFloatRequested, this, [this]() { handleFloat(); });
    connect(m_shortcutManager.get(), &ShortcutManager::swapWindowRequested, this, [this](NavigationDirection d) { handleSwap(d); });
    connect(m_shortcutManager.get(), &ShortcutManager::rotateWindowsRequested, this, [this](bool cw) { handleRotate(cw); });
    connect(m_shortcutManager.get(), &ShortcutManager::snapToZoneRequested, this, [this](int n) { handleSnap(n); });
    connect(m_shortcutManager.get(), &ShortcutManager::cycleWindowsInZoneRequested, this, [this](bool fwd) { handleCycle(fwd); });
    connect(m_shortcutManager.get(), &ShortcutManager::resnapToNewLayoutRequested, this, [this]() { handleResnap(); });
    connect(m_shortcutManager.get(), &ShortcutManager::snapAllWindowsRequested, this, [this]() { handleSnapAll(); });

    // Initialize mode tracker for last-used layout
    m_modeTracker = std::make_unique<ModeTracker>(m_settings.get(), this);
    m_modeTracker->load();

    // Reconcile persisted mode with actual state: if the mode tracker says
    // Autotile but no screen has an autotile assignment (or the feature is
    // disabled), force back to Manual so the popup shows the right layouts.
    if (m_modeTracker->isAutotileMode()) {
        bool hasAutotileAssignment = false;
        if (m_settings->autotileEnabled() && m_layoutManager) {
            for (QScreen *screen : Utils::allScreens()) {
                const QString screenId = Utils::screenIdentifier(screen);
                const int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
                const QString activity = (m_activityManager && ActivityManager::isAvailable())
                                         ? m_activityManager->currentActivity() : QString();
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
        connect(m_autotileEngine.get(), &AutotileEngine::algorithmChanged,
                this, [this](const QString& algorithmId) {
            if (m_modeTracker) {
                m_modeTracker->recordAutotileAlgorithm(algorithmId);
            }
            // Only show OSD when actually in autotile mode — loadState() emits
            // algorithmChanged during startup even if we're in manual mode.
            // Use layout OSD (visual zone preview) when changing algorithm, same as manual layout switch.
            if (m_modeTracker && m_modeTracker->isAutotileMode()
                && m_settings && m_settings->showOsdOnLayoutSwitch() && m_overlayService) {
                auto *algo = AlgorithmRegistry::instance()->algorithm(algorithmId);
                QString displayName = algo ? algo->name() : algorithmId;
                QString screenName = m_unifiedLayoutController ? m_unifiedLayoutController->currentScreenName() : QString();
                if (screenName.isEmpty() && m_windowTrackingAdaptor) {
                    if (QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor)) {
                        screenName = Utils::screenIdentifier(screen);
                    }
                }
                showLayoutOsdForAlgorithm(algorithmId, displayName, screenName);
            }
        });

        // Sync autotile float state and show OSD when a window is floated/unfloated
        connect(m_autotileEngine.get(), &AutotileEngine::windowFloatingChanged,
                this, [this](const QString& windowId, bool floating, const QString& screenName) {
            // F1+F2 fix: Sync floating state to WindowTrackingService and propagate
            // to KWin effect's NavigationHandler::m_floatingWindows via D-Bus signal.
            // WTA::setWindowFloating() calls WTS::setWindowFloating() + emits D-Bus signal.
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->setWindowFloating(windowId, floating);
            }
            // Save pre-float zone assignment for restore on unfloat
            if (floating && m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->service()->unsnapForFloat(windowId);
            }

            // When floating: restore pre-autotile geometry (KWin syncs it via recordPreAutotileGeometry)
            if (floating && m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->applyGeometryForFloat(windowId, screenName);
            }

            // Use "Floating" and "Tiled" labels for autotile (not "Snapped" for unfloat)
            if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
                QString reason = floating ? QStringLiteral("floated") : QStringLiteral("tiled");
                m_overlayService->showNavigationOsd(true, QStringLiteral("float"), reason,
                                                    QString(), QString(), screenName);
            }
        });

        // When windows are released from autotile (screens removed from autotile
        // via assignment change, not just toggle shortcut), resnap them back to
        // their pre-autotile zone positions so they don't remain stuck at tiled geometry.
        connect(m_autotileEngine.get(), &AutotileEngine::windowsReleasedFromTiling,
                this, [this](const QStringList& windowIds) {
            Q_UNUSED(windowIds)
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->resnapCurrentAssignments();
            }
        });

        // ═══════════════════════════════════════════════════════════════════════════
        // Autotile Shortcut Signals
        // ═══════════════════════════════════════════════════════════════════════════

        connect(m_shortcutManager.get(), &ShortcutManager::toggleAutotileRequested,
                this, [this]() {
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
            int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
            QString activity = (m_activityManager && ActivityManager::isAvailable())
                               ? m_activityManager->currentActivity() : QString();

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
            if (wasAutotile) {
                // Currently autotile → switch to last manual layout
                QString layoutId = m_modeTracker->lastManualLayoutId();
                if (!layoutId.isEmpty()) {
                    applied = m_unifiedLayoutController->applyLayoutById(layoutId);
                }
            } else {
                // Currently manual → switch to last autotile algorithm
                QString algoId = m_modeTracker->lastAutotileAlgorithm();
                if (!algoId.isEmpty()) {
                    applied = m_unifiedLayoutController->applyLayoutById(LayoutId::makeAutotileId(algoId));
                }
            }

            // If apply failed (e.g. layout was deleted), restore the correct filter
            if (!applied) {
                updateLayoutFilter();
            }

            // When switching autotile→manual, windows need to be moved back to
            // their pre-autotile zone positions. Autotile bypasses the normal
            // windowSnapped tracking, so m_windowZoneAssignments still holds
            // the zone assignments from before autotile was enabled.
            // Filter by connector name (screen->name()) to match what KWin
            // stores in m_windowScreenAssignments — only resnap windows on the
            // screen that was actually toggled.
            if (applied && wasAutotile && m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->resnapCurrentAssignments(screen->name());
            }
        });

        connect(m_shortcutManager.get(), &ShortcutManager::focusMasterRequested, this, [this]() { handleFocusMaster(); });
        connect(m_shortcutManager.get(), &ShortcutManager::swapWithMasterRequested, this, [this]() { handleSwapWithMaster(); });
        connect(m_shortcutManager.get(), &ShortcutManager::increaseMasterRatioRequested, this, [this]() { handleIncreaseMasterRatio(); });
        connect(m_shortcutManager.get(), &ShortcutManager::decreaseMasterRatioRequested, this, [this]() { handleDecreaseMasterRatio(); });
        connect(m_shortcutManager.get(), &ShortcutManager::increaseMasterCountRequested, this, [this]() { handleIncreaseMasterCount(); });
        connect(m_shortcutManager.get(), &ShortcutManager::decreaseMasterCountRequested, this, [this]() { handleDecreaseMasterCount(); });
        connect(m_shortcutManager.get(), &ShortcutManager::retileRequested, this, [this]() { handleRetile(); });
    }

    // Initialize unified layout controller (manual layouts only)
    m_unifiedLayoutController = std::make_unique<UnifiedLayoutController>(
        m_layoutManager.get(), m_settings.get(), m_autotileEngine.get(), this);

    // Set initial desktop/activity context for visibility-filtered cycling
    m_layoutManager->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());
    m_unifiedLayoutController->setCurrentVirtualDesktop(m_virtualDesktopManager->currentDesktop());
    if (m_activityManager && ActivityManager::isAvailable()) {
        m_layoutManager->setCurrentActivity(m_activityManager->currentActivity());
        m_unifiedLayoutController->setCurrentActivity(m_activityManager->currentActivity());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Mode-based layout filtering
    // ═══════════════════════════════════════════════════════════════════════════

    // Derive initial per-screen autotile state from assignments
    updateAutotileScreens();

    // Set initial layout filter
    updateLayoutFilter();

    // Update layout filter when tiling mode changes at runtime
    connect(m_modeTracker.get(), &ModeTracker::currentModeChanged, this, &Daemon::updateLayoutFilter);

    // Feature gate: when KCM checkbox changes, enforce constraints
    connect(m_settings.get(), &Settings::autotileEnabledChanged, this, [this]() {
        if (!m_settings) {
            return;
        }

        if (!m_settings->autotileEnabled()) {
            // Feature disabled: clear all autotile assignments
            if (m_layoutManager) {
                m_layoutManager->clearAutotileAssignments();
            }
            updateAutotileScreens();
            // Restore last manual layout so windows aren't stuck in tiled positions
            if (m_modeTracker && m_layoutManager) {
                const QString lastLayoutId = m_modeTracker->lastManualLayoutId();
                if (!lastLayoutId.isEmpty()) {
                    Layout* layout = m_layoutManager->layoutById(QUuid::fromString(lastLayoutId));
                    if (layout) {
                        m_layoutManager->setActiveLayout(layout);
                    }
                }
            }
            // Resnap windows back to their pre-autotile zone positions (all screens)
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->resnapCurrentAssignments();
            }
        }

        updateLayoutFilter();
    });

    // Re-derive autotile screens when assignments change
    connect(m_layoutManager.get(), &LayoutManager::layoutAssigned, this, [this]() {
        updateAutotileScreens();
    });

    // Connect unified layout controller signals for OSD display and mode tracking
    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::layoutApplied, this, [this](Layout* layout) {
        if (m_modeTracker) {
            m_modeTracker->setCurrentMode(TilingMode::Manual);
        }
        if (m_settings && m_settings->showOsdOnLayoutSwitch()) {
            showLayoutOsd(layout, m_unifiedLayoutController->currentScreenName());
        }
    });

    connect(m_unifiedLayoutController.get(), &UnifiedLayoutController::autotileApplied,
            this, [this](const QString& algorithmName, int windowCount) {
        Q_UNUSED(windowCount)
        if (m_modeTracker) {
            m_modeTracker->setCurrentMode(TilingMode::Autotile);
        }
        // Use layout OSD (visual zone preview) when applying autotile, same as manual layout switch.
        if (m_settings && m_settings->showOsdOnLayoutSwitch() && m_autotileEngine && m_overlayService) {
            QString algorithmId = m_autotileEngine->algorithm();
            QString screenName = m_unifiedLayoutController->currentScreenName();
            showLayoutOsdForAlgorithm(algorithmId, algorithmName, screenName);
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
    connect(m_overlayService.get(), &IOverlayService::snapAssistWindowSelected, this,
            [this](const QString& windowId, const QString& zoneId, const QString& geometryJson,
                   const QString& screenName) {
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
                    QString authGeometry =
                        m_windowTrackingAdaptor->getZoneGeometryForScreen(zoneId, effectiveScreen);
                    if (!authGeometry.isEmpty()) {
                        geometryToUse = authGeometry;
                    }
                }
                m_windowTrackingAdaptor->requestMoveSpecificWindowToZone(windowId, zoneId, geometryToUse);
            });

    // Connect navigation feedback signal to show OSD (manual mode: from WindowTrackingAdaptor via KWin effect)
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::navigationFeedback, this,
            [this](bool success, const QString& action, const QString& reason,
                   const QString& sourceZoneId, const QString& targetZoneId, const QString& screenName) {
                if (m_settings && m_settings->showNavigationOsd()) {
                    m_overlayService->showNavigationOsd(success, action, reason, sourceZoneId, targetZoneId, screenName);
                }
            });

    // Connect autotile navigation feedback (same OSD path: shortcut → operation → OSD)
    connect(m_autotileEngine.get(), &AutotileEngine::navigationFeedbackRequested, this,
            [this](bool success, const QString& action, const QString& reason,
                   const QString& sourceZoneId, const QString& targetZoneId, const QString& screenName) {
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

    // Restore autotile state from previous session (window order, algorithm, split ratio)
    // Defers actual retiling until windows are announced by KWin effect
    if (m_autotileEngine) {
        m_autotileEngine->loadState();
    }

    m_running = true;

    // Signal that daemon is fully initialized and ready for queries
    Q_EMIT m_layoutAdaptor->daemonReady();
}

void Daemon::stop()
{
    if (!m_running) {
        return;
    }

    // Stop pending timers to prevent callbacks during shutdown
    m_geometryUpdateTimer.stop();
    m_pendingGeometryUpdates.clear();

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

    m_reapplyGeometriesTimer.stop();

    // Save autotile state and clear active screens
    if (m_autotileEngine) {
        m_autotileEngine->saveState();
        m_autotileEngine->setAutotileScreens({});
    }

    // Clear the adaptor's raw engine pointer BEFORE destroying the engine.
    // The adaptor is a Qt child of the daemon (destroyed later); a D-Bus call
    // arriving between engine destruction and adaptor destruction would otherwise
    // access freed memory. After clearing, ensureEngine() returns false.
    if (m_autotileAdaptor) {
        m_autotileAdaptor->clearEngine();
    }

    // Null the WindowDragAdaptor's engine pointer for the same reason.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setAutotileEngine(nullptr);
    }

    // Destroy the engine now (during stop(), before Qt child destruction order).
    m_autotileEngine.reset();

    // Unregister D-Bus service to prevent late calls during shutdown
    QDBusConnection::sessionBus().unregisterService(QString(DBus::ServiceName));

    m_running = false;
}

void Daemon::showOverlay()
{
    // Don't show overlay when all screens are in autotile mode
    // (the overlay is for manual zone selection during drag)
    if (m_autotileEngine && m_screenManager) {
        const auto& autotileScreens = m_autotileEngine->autotileScreens();
        if (!autotileScreens.isEmpty()) {
            bool allAutotile = true;
            for (QScreen* screen : m_screenManager->screens()) {
                if (!autotileScreens.contains(screen->name())) {
                    allAutotile = false;
                    break;
                }
            }
            if (allAutotile) {
                return;
            }
        }
    }
    // Per-screen autotile exclusion is handled by OverlayService::initializeOverlay()
    // via m_excludedScreens (set in updateAutotileScreens)
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

void Daemon::showLayoutOsdForAlgorithm(const QString& algorithmId, const QString& displayName,
                                       const QString& screenName)
{
    auto *algo = AlgorithmRegistry::instance()->algorithm(algorithmId);
    if (!algo) {
        qCWarning(lcDaemon) << "Algorithm not found for OSD:" << algorithmId;
        return;
    }

    OsdStyle style = m_settings ? m_settings->osdStyle() : OsdStyle::Preview;

    switch (style) {
    case OsdStyle::None:
        qCInfo(lcDaemon) << "OSD disabled, skipping for algorithm:" << displayName;
        return;

    case OsdStyle::Text:
        {
            QDBusMessage msg = QDBusMessage::createMethodCall(
                QStringLiteral("org.kde.plasmashell"), QStringLiteral("/org/kde/osdService"),
                QStringLiteral("org.kde.osdService"), QStringLiteral("showText"));

            QString displayText = i18n("Zone Layout: %1", displayName);
            msg << QStringLiteral("plasmazones") << displayText;

            QDBusConnection::sessionBus().asyncCall(msg);
            qCInfo(lcDaemon) << "Showing text OSD for algorithm:" << displayName;
        }
        break;

    case OsdStyle::Preview:
        if (m_overlayService) {
            QVariantList zones = AlgorithmRegistry::generatePreviewZones(algo);
            QString layoutId = LayoutId::makeAutotileId(algorithmId);
            m_overlayService->showLayoutOsd(layoutId, displayName, zones,
                                            static_cast<int>(LayoutCategory::Autotile),
                                            false, screenName);
            qCInfo(lcDaemon) << "Showing preview OSD for algorithm:" << displayName << "on screen:" << screenName;
        } else {
            qCWarning(lcDaemon) << "Overlay service not available for preview OSD";
        }
        break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Navigation handlers — single code path per operation (DRY/SOLID)
// Resolve screen → check mode (autotile vs zones) → delegate → OSD from backend
// ═══════════════════════════════════════════════════════════════════════════════

void Daemon::handleRotate(bool clockwise)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for rotate shortcut — skipping";
        return;
    }
    // Use connector name: m_autotileScreens and m_windowScreenAssignments both use screen->name()
    QString screenName = screen->name();
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screenName)) {
        m_autotileEngine->rotateWindowOrder(clockwise);
    } else {
        m_windowTrackingAdaptor->rotateWindowsInLayout(clockwise, screenName);
    }
}

void Daemon::handleFloat()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        m_autotileEngine->toggleFocusedWindowFloat();
    } else {
        m_windowTrackingAdaptor->toggleWindowFloat();
    }
}

void Daemon::handleMove(NavigationDirection direction)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen || (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name()))) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown move navigation direction:" << static_cast<int>(direction);
        return;
    }
    m_windowTrackingAdaptor->moveWindowToAdjacentZone(dirStr);
}

void Daemon::handleFocus(NavigationDirection direction)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen || (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name()))) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown focus navigation direction:" << static_cast<int>(direction);
        return;
    }
    m_windowTrackingAdaptor->focusAdjacentZone(dirStr);
}

void Daemon::handlePush()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for pushToEmptyZone shortcut — skipping";
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        return;
    }
    m_windowTrackingAdaptor->pushToEmptyZone(screen->name());
}

void Daemon::handleRestore()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen || (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name()))) {
        return;
    }
    m_windowTrackingAdaptor->restoreWindowSize();
}

void Daemon::handleSwap(NavigationDirection direction)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen || (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name()))) {
        return;
    }
    QString dirStr = navigationDirectionToString(direction);
    if (dirStr.isEmpty()) {
        qCWarning(lcDaemon) << "Unknown swap navigation direction:" << static_cast<int>(direction);
        return;
    }
    m_windowTrackingAdaptor->swapWindowWithAdjacentZone(dirStr);
}

void Daemon::handleSnap(int zoneNumber)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for snapToZone shortcut — skipping";
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        return;
    }
    m_windowTrackingAdaptor->snapToZoneByNumber(zoneNumber, screen->name());
}

void Daemon::handleCycle(bool forward)
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen || (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name()))) {
        return;
    }
    m_windowTrackingAdaptor->cycleWindowsInZone(forward);
}

void Daemon::handleResnap()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen || (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name()))) {
        return;
    }
    m_windowTrackingAdaptor->resnapToNewLayout();
}

void Daemon::handleSnapAll()
{
    QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
    if (!screen) {
        qCDebug(lcDaemon) << "No screen info for snapAllWindows shortcut — skipping";
        return;
    }
    if (m_autotileEngine && m_autotileEngine->isAutotileScreen(screen->name())) {
        return;
    }
    m_windowTrackingAdaptor->snapAllWindows(screen->name());
}

void Daemon::handleFocusMaster()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->focusMaster();
}

void Daemon::handleSwapWithMaster()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->swapFocusedWithMaster();
}

void Daemon::handleIncreaseMasterRatio()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->increaseMasterRatio();
}

void Daemon::handleDecreaseMasterRatio()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->decreaseMasterRatio();
}

void Daemon::handleIncreaseMasterCount()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->increaseMasterCount();
}

void Daemon::handleDecreaseMasterCount()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->decreaseMasterCount();
}

void Daemon::handleRetile()
{
    if (!m_autotileEngine || !m_autotileEngine->isEnabled()) {
        return;
    }
    m_autotileEngine->retile();
    if (m_settings && m_settings->showNavigationOsd() && m_overlayService) {
        QScreen* screen = resolveShortcutScreen(m_windowTrackingAdaptor);
        QString screenName = screen ? screen->name() : QString();
        if (screenName.isEmpty() && !m_autotileEngine->autotileScreens().isEmpty()) {
            screenName = *m_autotileEngine->autotileScreens().begin();
        }
        m_overlayService->showNavigationOsd(true, QStringLiteral("retile"), QStringLiteral("retiled"),
                                           QString(), QString(), screenName);
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

void Daemon::updateLayoutFilter()
{
    if (!m_settings) {
        return;
    }

    // Exclusive mode based on runtime tiling mode, gated by the feature toggle.
    // autotileEnabled is the feature gate (can autotile be used at all?).
    // ModeTracker tracks what's actually active right now.
    const bool autotileActive = m_settings->autotileEnabled()
                                && m_modeTracker && m_modeTracker->isAutotileMode();
    const bool includeManual = !autotileActive;
    const bool includeAutotile = autotileActive;

    if (m_overlayService) {
        m_overlayService->setLayoutFilter(includeManual, includeAutotile);
    }
    if (m_unifiedLayoutController) {
        m_unifiedLayoutController->setLayoutFilter(includeManual, includeAutotile);
    }

    qCDebug(lcDaemon) << "Layout filter updated: manual=" << includeManual
                       << "autotile=" << includeAutotile;
}

void Daemon::updateAutotileScreens()
{
    if (!m_autotileEngine || !m_layoutManager || !m_screenManager) {
        return;
    }

    const int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    const QString activity = (m_activityManager && ActivityManager::isAvailable())
                             ? m_activityManager->currentActivity() : QString();

    QSet<QString> autotileScreens;
    for (QScreen* screen : m_screenManager->screens()) {
        QString screenId = Utils::screenIdentifier(screen);
        QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
        if (LayoutId::isAutotile(assignmentId)) {
            autotileScreens.insert(screen->name());
        }
    }

    m_autotileEngine->setAutotileScreens(autotileScreens);

    // Propagate to overlay service so initializeOverlay() skips autotile screens
    if (m_overlayService) {
        m_overlayService->setExcludedScreens(autotileScreens);
    }

    qCDebug(lcDaemon) << "Updated autotile screens:" << autotileScreens;
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
        QString screenId = Utils::screenIdForName(screenName);
        if (Layout* screenLayout =
                m_layoutManager->layoutForScreen(screenId, currentDesktop, currentActivity)) {
            if (screenLayout != m_layoutManager->activeLayout()) {
                screenLayout->recalculateZoneGeometries(availableGeometry);
            }
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

    // Retile autotile windows to adapt to new screen geometry
    // (panels added/removed, resolution changes, etc.)
    if (m_autotileEngine && m_autotileEngine->isEnabled()) {
        m_autotileEngine->retile();
    }
}

} // namespace PlasmaZones
