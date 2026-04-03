// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "cavaservice.h"
#include "windowthumbnailservice.h"

#include "../core/layout.h"
#include "../core/layoutmanager.h"
#include "../core/zone.h"
#include "../core/layoututils.h"
#include "../core/geometryutils.h"
#include "../core/screenmanager.h"
#include "../core/utils.h"
#include "../core/constants.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QMutexLocker>

#include "../core/logging.h"
#include "pz_qml_i18n.h"
#include "vulkan_support.h"

#include "../core/layersurface.h"

namespace PlasmaZones {

namespace {

// Clean up all windows in a QHash map
template<typename K>
void cleanupWindowMap(QHash<K, QQuickWindow*>& windowMap)
{
    for (auto* window : std::as_const(windowMap)) {
        if (window) {
            QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);
            window->close();
            window->destroy();
            window->deleteLater();
        }
    }
    windowMap.clear();
}

} // namespace

OverlayService::OverlayService(QObject* parent)
    : IOverlayService(parent)
    , m_engine(std::make_unique<QQmlEngine>()) // No parent - unique_ptr manages lifetime
{
    // Set up i18n for QML (makes i18n() available in QML)
    auto* localizedContext = new PzLocalizedContext(m_engine.get());
    m_engine->rootContext()->setContextObject(localizedContext);

    // Connect to screen changes (with safety check for early initialization)
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);
    } else {
        qCWarning(lcOverlay) << "Overlay: created before QGuiApplication, screen signals not connected";
    }

    // Connect to virtual screen configuration changes
    if (auto* mgr = ScreenManager::instance()) {
        connect(mgr, &ScreenManager::virtualScreensChanged, this, [this](const QString& physicalScreenId) {
            // Destroy old overlays for this physical screen, recreate with new config
            QScreen* physScreen = Utils::findScreenByIdOrName(physicalScreenId);
            if (!physScreen) {
                // Physical screen removed -- clean up stale virtual screen entries
                const QString prefix = physicalScreenId + VirtualScreenId::separator();
                for (auto it = m_overlayWindows.begin(); it != m_overlayWindows.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_overlayWindows.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_overlayPhysScreens.begin(); it != m_overlayPhysScreens.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_overlayPhysScreens.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_overlayGeometries.begin(); it != m_overlayGeometries.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_overlayGeometries.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_zoneSelectorWindows.begin(); it != m_zoneSelectorWindows.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_zoneSelectorWindows.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_zoneSelectorPhysScreens.begin(); it != m_zoneSelectorPhysScreens.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_zoneSelectorPhysScreens.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_layoutOsdWindows.begin(); it != m_layoutOsdWindows.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_layoutOsdWindows.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_layoutOsdPhysScreens.begin(); it != m_layoutOsdPhysScreens.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_layoutOsdPhysScreens.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_navigationOsdWindows.begin(); it != m_navigationOsdWindows.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_navigationOsdWindows.erase(it);
                    else
                        ++it;
                }
                for (auto it = m_navigationOsdPhysScreens.begin(); it != m_navigationOsdPhysScreens.end();) {
                    if (it.key().startsWith(prefix))
                        it = m_navigationOsdPhysScreens.erase(it);
                    else
                        ++it;
                }
                return;
            }

            // Clear selected zone before destroying windows — the selection references
            // zone geometry from the old virtual screen config and would be stale.
            clearSelectedZone();

            // Track whether zone selectors were visible before destruction so we can
            // recreate them for the new virtual screen configuration.
            const bool hadZoneSelector = m_zoneSelectorVisible;

            // Destroy all window types (overlays, selectors, OSDs, snap assist, layout picker)
            destroyAllWindowsForPhysicalScreen(physScreen);

            // Reset zone selector flag — the windows were destroyed, so the flag
            // must be cleared to allow re-showing. Without this, the guard at the
            // top of showZoneSelector() prevents recreation.
            if (hadZoneSelector) {
                m_zoneSelectorVisible = false;
            }

            // Recreate with new virtual screen config if visible
            if (isVisible()) {
                auto* mgr2 = ScreenManager::instance();
                if (mgr2 && mgr2->hasVirtualScreens(physicalScreenId)) {
                    for (const QString& vsId : mgr2->virtualScreenIdsFor(physicalScreenId)) {
                        QRect vsGeom = mgr2->screenGeometry(vsId);
                        if (vsGeom.isValid()) {
                            createOverlayWindow(vsId, physScreen, vsGeom);
                        }
                    }
                } else {
                    createOverlayWindow(physScreen);
                }
            }

            // Recreate zone selectors for the new virtual screen configuration
            if (hadZoneSelector) {
                showZoneSelector();
            }
        });
    }

    // Connect to system sleep/resume via logind to restart shader timer after wake
    // This prevents large iTimeDelta jumps when system resumes from sleep
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.login1"),
                                         QStringLiteral("/org/freedesktop/login1"),
                                         QStringLiteral("org.freedesktop.login1.Manager"),
                                         QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));

    // Reset shader error state on construction (fresh start after reboot)
    m_pendingShaderError.clear();

    m_cavaService = std::make_unique<CavaService>(this);
    connect(m_cavaService.get(), &CavaService::spectrumUpdated, this, &OverlayService::onAudioSpectrumUpdated);

    // Create a persistent 1x1 keep-alive window to prevent Qt from tearing down
    // global Wayland/Vulkan protocol objects (zwp_linux_dmabuf_v1, wp_presentation,
    // etc.) when the last visible QQuickWindow is destroyed. Without this, after
    // overlay destroy-on-hide, Qt cleans up the Vulkan rendering infrastructure
    // and no new windows can render buffers.
    QTimer::singleShot(0, this, [this]() {
        QScreen* screen = Utils::primaryScreen();
        if (!screen) {
            return;
        }
        m_keepAliveWindow = new QQuickWindow();
        QQmlEngine::setObjectOwnership(m_keepAliveWindow, QQmlEngine::CppOwnership);
#if QT_CONFIG(vulkan)
        auto* vulkanInstance = qApp->property(PlasmaZones::PzVulkanInstanceProperty).value<QVulkanInstance*>();
        if (vulkanInstance) {
            m_keepAliveWindow->setVulkanInstance(vulkanInstance);
        }
#endif
        m_keepAliveWindow->setScreen(screen);
        m_keepAliveWindow->setWidth(1);
        m_keepAliveWindow->setHeight(1);
        // Configure as background layer — invisible, no input, minimal compositor cost.
        // Best-effort: if layer-shell is unavailable the window still keeps Qt's
        // Vulkan globals alive (just renders as a tiny xdg_toplevel).
        (void)configureLayerSurface(m_keepAliveWindow, screen, LayerSurface::LayerBackground,
                                    LayerSurface::KeyboardInteractivityNone, QStringLiteral("plasmazones-keepalive"),
                                    LayerSurface::AnchorNone, 0);
        m_keepAliveWindow->show();
    });
}

bool OverlayService::isVisible() const
{
    return m_visible;
}

bool OverlayService::isZoneSelectorVisible() const
{
    return m_zoneSelectorVisible;
}

OverlayService::~OverlayService()
{
    // Clear the Vulkan instance app property before destruction to avoid dangling pointer.
    // Only needed for the fallback instance we own — when main.cpp provided the instance,
    // it outlives OverlayService (declared before QGuiApplication), so no cleanup needed.
#if QT_CONFIG(vulkan)
    if (m_fallbackVulkanInstance && qGuiApp) {
        qApp->setProperty(PlasmaZones::PzVulkanInstanceProperty, QVariant());
    }
#endif

    // Disconnect from QGuiApplication first so we don't get screen-related callbacks
    // while we're destroying windows.
    if (qGuiApp) {
        disconnect(qGuiApp, nullptr, this, nullptr);
    }

    // Clean up all window types before engine is destroyed
    // (takes C++ ownership to prevent QML GC interference)
    cleanupWindowMap(m_zoneSelectorWindows);
    cleanupWindowMap(m_overlayWindows);
    cleanupWindowMap(m_layoutOsdWindows);
    cleanupWindowMap(m_navigationOsdWindows);

    // Destroy keep-alive window last (after all other windows)
    if (m_keepAliveWindow) {
        m_keepAliveWindow->close();
        m_keepAliveWindow->destroy();
        delete m_keepAliveWindow;
        m_keepAliveWindow = nullptr;
    }

    // Process pending deletions before destroying the QML engine.
    // All deleteLater() calls must complete while the engine is still valid.
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Now m_engine (unique_ptr) will be destroyed safely
    // since all QML objects have been properly cleaned up
}

QQuickWindow* OverlayService::createQmlWindow(const QUrl& qmlUrl, QScreen* screen, const char* windowType,
                                              const QVariantMap& initialProperties)
{
    if (!screen) {
        qCWarning(lcOverlay) << "Screen is null for" << windowType;
        return nullptr;
    }

    QQmlComponent component(m_engine.get(), qmlUrl);

    if (component.isError()) {
        qCWarning(lcOverlay) << "Failed to load" << windowType << "QML:" << component.errors();
        return nullptr;
    }

    if (component.status() != QQmlComponent::Ready) {
        qCWarning(lcOverlay) << windowType << "QML component not ready, status=" << component.status();
        return nullptr;
    }

    QObject* obj =
        initialProperties.isEmpty() ? component.create() : component.createWithInitialProperties(initialProperties);
    if (!obj) {
        qCWarning(lcOverlay) << "Failed to create" << windowType << "window:" << component.errors();
        return nullptr;
    }

    auto* window = qobject_cast<QQuickWindow*>(obj);
    if (!window) {
        qCWarning(lcOverlay) << "Created object is not a QQuickWindow for" << windowType;
        obj->deleteLater();
        return nullptr;
    }

    // Take C++ ownership so QML's GC doesn't delete the window
    QQmlEngine::setObjectOwnership(window, QQmlEngine::CppOwnership);

    // Enable persistent RHI pipeline cache — compiled GPU programs survive across sessions.
    // This eliminates shader recompilation on subsequent daemon starts.
    // Must be called before the window is shown (before scene graph initialization).
    // All windows share the same cache file — the RHI pipeline cache format is window-agnostic
    // and Qt serializes writes, so a single shared file is both correct and efficient.
    if (!m_pipelineCacheConfigured) {
        const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheDir.isEmpty()) {
            QDir().mkpath(cacheDir);
            QQuickGraphicsConfiguration config = window->graphicsConfiguration();
            config.setPipelineCacheSaveFile(cacheDir + QStringLiteral("/plasmazones-pipeline.cache"));
            window->setGraphicsConfiguration(config);
            m_pipelineCacheConfigured = true;
        }
    }

    // When the Vulkan backend is active, each QQuickWindow needs a QVulkanInstance
    // set before it can create a Vulkan surface. The instance is stored as a dynamic
    // property on QGuiApplication by main.cpp.
    // IMPORTANT: setVulkanInstance() must be called before the window is shown or
    // the scene graph is initialized. This is safe here because the window starts
    // with visible: false and show() is called separately after createQmlWindow().
#if QT_CONFIG(vulkan)
    auto* vulkanInstance = qApp->property(PlasmaZones::PzVulkanInstanceProperty).value<QVulkanInstance*>();
    if (vulkanInstance) {
        window->setVulkanInstance(vulkanInstance);
    } else if (QQuickWindow::graphicsApi() == QSGRendererInterface::Vulkan) {
        // This can happen when backend is 'auto' and Qt chose Vulkan at runtime.
        // Create a QVulkanInstance on-the-fly so overlays still work.
        if (!m_fallbackVulkanInstance) {
            m_fallbackVulkanInstance = std::make_unique<QVulkanInstance>();
            m_fallbackVulkanInstance->setApiVersion(PlasmaZones::PzVulkanApiVersion);
            if (m_fallbackVulkanInstance->create()) {
                qCInfo(lcOverlay) << "Created fallback QVulkanInstance for 'auto' backend (Qt chose Vulkan).";
                qApp->setProperty(PlasmaZones::PzVulkanInstanceProperty,
                                  QVariant::fromValue(m_fallbackVulkanInstance.get()));
            } else {
                qCCritical(lcOverlay) << "Failed to create fallback QVulkanInstance."
                                      << "Overlay windows will not render correctly.";
                m_fallbackVulkanInstance.reset();
                window->deleteLater();
                return nullptr;
            }
        }
        window->setVulkanInstance(m_fallbackVulkanInstance.get());
    }
#endif

    // Set the screen before the QPA plugin creates the LayerSurface
    window->setScreen(screen);

    return window;
}

void OverlayService::show()
{
    if (m_visible) {
        return;
    }

    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor
        cursorScreen = QGuiApplication::screenAt(QCursor::pos());
        if (!cursorScreen) {
            // Fallback to primary screen if cursor position detection fails
            cursorScreen = Utils::primaryScreen();
        }
        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        // Check both physical and effective (virtual) screen IDs
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(QCursor::pos(), cursorScreen);
            if (isContextDisabled(m_settings, effectiveId, m_currentVirtualDesktop, m_currentActivity)) {
                return;
            }
        }
    }

    initializeOverlay(cursorScreen);
}

void OverlayService::showAtPosition(int cursorX, int cursorY)
{
    // Check if we should show on all monitors or just the cursor's screen
    bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();

    QScreen* cursorScreen = nullptr;
    if (!showOnAllMonitors) {
        // Find the screen containing the cursor using provided coordinates
        // This works on Wayland where QCursor::pos() doesn't work
        cursorScreen = Utils::findScreenAtPosition(cursorX, cursorY);
        if (!cursorScreen) {
            // Fallback to primary screen if no screen contains the cursor position
            cursorScreen = Utils::primaryScreen();
        }

        // If the cursor's screen has PlasmaZones disabled, don't show overlay at all
        // Check both physical and effective (virtual) screen IDs
        if (cursorScreen && m_settings) {
            QString effectiveId = Utils::effectiveScreenIdAt(QPoint(cursorX, cursorY), cursorScreen);
            if (isContextDisabled(m_settings, effectiveId, m_currentVirtualDesktop, m_currentActivity)) {
                return;
            }
        }
    }

    const QPoint cursorPos(cursorX, cursorY);

    if (m_visible) {
        // Already visible: when single-monitor mode, switch overlay if cursor moved to different screen (#136)
        // Use effective (virtual-aware) screen ID to detect cross-virtual-screen movement
        QString cursorEffectiveId = Utils::effectiveScreenIdAt(QPoint(cursorX, cursorY), cursorScreen);
        if (!showOnAllMonitors && !cursorEffectiveId.isEmpty() && m_currentOverlayScreenId != cursorEffectiveId) {
            initializeOverlay(cursorScreen, cursorPos);
        }
        return;
    }

    initializeOverlay(cursorScreen, cursorPos);
}

void OverlayService::hide()
{
    if (!m_visible) {
        return;
    }

    m_visible = false;
    m_currentOverlayScreenId.clear();

    // Stop shader animation
    stopShaderAnimation();

    // Do NOT invalidate m_shaderTimer - keeps iTime continuous across show/hide
    // so animations feel less predictable and don't restart

    // Destroy overlay windows instead of hiding them. On Vulkan with Wayland
    // layer-shell, window->hide() destroys the wl_surface but the Qt Vulkan
    // backend doesn't properly reinitialize the VkSwapchainKHR when the window
    // is re-shown, causing the scene graph render loop to stall. Destroying the
    // window entirely and creating a fresh one on the next show() avoids this.
    // initializeOverlay() will call createOverlayWindow() since m_overlayWindows
    // is now empty.
    const QStringList screenIds = m_overlayWindows.keys();
    for (const QString& screenId : screenIds) {
        destroyOverlayWindow(screenId);
    }

    m_pendingShaderError.clear();

    Q_EMIT visibilityChanged(false);
}

void OverlayService::toggle()
{
    if (m_visible) {
        hide();
    } else {
        show();
    }
}

void OverlayService::updateSettings(ISettings* settings)
{
    setSettings(settings);

    // Sync CAVA state with current settings.  The signal-based handlers
    // (enableAudioVisualizerChanged, etc.) connected in setSettings() only
    // fire when load() detects a value change.  When the KCM uses batch
    // setSettings + reloadSettings, the in-memory values are already updated
    // by the batch setters before load() runs, so load() sees no change and
    // the signals never fire.  Syncing here ensures CAVA always reflects
    // the current configuration.
    syncCavaState();

    // Hide overlay and zone selector on disabled screens/desktops/activities,
    // then refresh remaining (non-disabled) windows with the new settings.
    hideDisabledAndRefresh();

    // If the selector was visible but got disabled via settings, hide it immediately.
    if (m_zoneSelectorVisible && m_settings && !m_settings->zoneSelectorEnabled()) {
        hideZoneSelector();
    }
}

void OverlayService::setLayout(Layout* layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        // Mark zone data as dirty when layout changes to ensure shader overlay updates
        m_zoneDataDirty = true;
    }
}

Layout* OverlayService::resolveScreenLayout(QScreen* screen) const
{
    // Physical QScreen* overload: derives screenId and delegates.
    // Callers with a known virtual screenId should use the QString overload directly.
    if (!screen) {
        return m_layout;
    }
    return resolveScreenLayout(Utils::screenIdentifier(screen));
}

Layout* OverlayService::resolveScreenLayout(const QString& screenId) const
{
    Layout* screenLayout = nullptr;
    if (m_layoutManager && !screenId.isEmpty()) {
        screenLayout = m_layoutManager->layoutForScreen(screenId, m_currentVirtualDesktop, m_currentActivity);
        if (!screenLayout) {
            screenLayout = m_layoutManager->defaultLayout();
        }
    }
    if (!screenLayout) {
        screenLayout = m_layout;
    }
    return screenLayout;
}

void OverlayService::hideDisabledAndRefresh()
{
    // Destroy windows on screens where the current context is disabled.
    // Destroy (not hide) to free GPU resources for permanently inactive contexts.
    if (m_settings) {
        {
            const QStringList selectorScreenIds = m_zoneSelectorWindows.keys();
            for (const QString& screenId : selectorScreenIds) {
                if (isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
                    destroyZoneSelectorWindow(screenId);
                }
            }
        }
        if (m_visible) {
            const QStringList overlayScreenIds = m_overlayWindows.keys();
            for (const QString& screenId : overlayScreenIds) {
                if (isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
                    destroyOverlayWindow(screenId);
                }
            }
        }
    }

    // Update remaining (non-disabled) zone selector windows
    if (!m_zoneSelectorWindows.isEmpty()) {
        for (const QString& screenId : m_zoneSelectorWindows.keys()) {
            if (!isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
                updateZoneSelectorWindow(screenId);
            }
        }
    }
    // Update remaining overlay windows when visible
    if (m_visible && !m_overlayWindows.isEmpty()) {
        for (const QString& screenId : m_overlayWindows.keys()) {
            if (!isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
                QScreen* physScreen = m_overlayPhysScreens.value(screenId);
                if (physScreen) {
                    updateOverlayWindow(screenId, physScreen);
                }
            }
        }
    }
}

void OverlayService::setCurrentVirtualDesktop(int desktop)
{
    if (m_currentVirtualDesktop != desktop) {
        m_currentVirtualDesktop = desktop;
        qCInfo(lcOverlay) << "Virtual desktop changed to" << desktop;
        hideDisabledAndRefresh();
    }
}

void OverlayService::setCurrentActivity(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        qCInfo(lcOverlay) << "Activity changed activity=" << activityId;
        hideDisabledAndRefresh();
    }
}

void OverlayService::setupForScreen(QScreen* screen)
{
    // Set up overlay windows for all effective screens on this physical screen
    auto* mgr = ScreenManager::instance();
    const QString physId = Utils::screenIdentifier(screen);
    if (mgr && mgr->hasVirtualScreens(physId)) {
        for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
            if (!m_overlayWindows.contains(vsId)) {
                QRect vsGeom = mgr->screenGeometry(vsId);
                if (!vsGeom.isValid()) {
                    qCWarning(lcOverlay) << "setupForScreen: invalid geometry for virtual screen" << vsId
                                         << "— skipping overlay creation";
                    continue;
                }
                createOverlayWindow(vsId, screen, vsGeom);
            }
        }
    } else {
        if (!m_overlayWindows.contains(physId)) {
            createOverlayWindow(screen);
        }
    }
}

void OverlayService::removeScreen(QScreen* screen)
{
    destroyOverlayWindow(screen);
}

void OverlayService::assertWindowOnScreen(QWindow* window, QScreen* screen, const QRect& geometry)
{
    if (!window || !screen) {
        return;
    }
    if (window->screen() != screen) {
        window->setScreen(screen);
    }
    // For virtual screens (geometry differs from physical), positioning is handled by
    // LayerShellQt margins. Calling setGeometry with absolute coordinates would override
    // those margins, causing double-positioning. Only set geometry for physical screens.
    const QRect targetGeom = geometry.isValid() ? geometry : screen->geometry();
    if (targetGeom == screen->geometry()) {
        window->setGeometry(targetGeom);
    }
    // Virtual screens: size is set by the caller; position is set by LayerShellQt margins.
}

void OverlayService::handleScreenAdded(QScreen* screen)
{
    if (!m_visible || !screen) {
        return;
    }
    const QString physScreenId = Utils::screenIdentifier(screen);

    auto* mgr = ScreenManager::instance();
    if (mgr && mgr->hasVirtualScreens(physScreenId)) {
        // Create overlays for each virtual screen on this physical screen
        for (const QString& vsId : mgr->virtualScreenIdsFor(physScreenId)) {
            if (isContextDisabled(m_settings, vsId, m_currentVirtualDesktop, m_currentActivity)) {
                continue;
            }
            QRect vsGeom = mgr->screenGeometry(vsId);
            if (vsGeom.isValid()) {
                createOverlayWindow(vsId, screen, vsGeom);
                updateOverlayWindow(vsId, screen);
                if (auto* window = m_overlayWindows.value(vsId)) {
                    assertWindowOnScreen(window, screen, vsGeom);
                    window->show();
                }
            }
        }
    } else {
        createOverlayWindow(screen);
        updateOverlayWindow(screen);
        if (auto* window = m_overlayWindows.value(physScreenId)) {
            assertWindowOnScreen(window, screen);
            window->show();
        }
    }
}

void OverlayService::destroyAllWindowsForPhysicalScreen(QScreen* screen)
{
    // Remove all overlay windows associated with this physical screen
    // (includes any virtual screens on this physical screen)
    const QStringList overlayIds = m_overlayWindows.keys();
    for (const QString& id : overlayIds) {
        if (m_overlayPhysScreens.value(id) == screen) {
            destroyOverlayWindow(id);
        }
    }

    // Remove zone selector windows for this physical screen
    const QStringList selectorIds = m_zoneSelectorWindows.keys();
    for (const QString& id : selectorIds) {
        if (m_zoneSelectorPhysScreens.value(id) == screen) {
            destroyZoneSelectorWindow(id);
        }
    }

    // Remove layout OSD windows for this physical screen
    const QStringList layoutOsdIds = m_layoutOsdWindows.keys();
    for (const QString& id : layoutOsdIds) {
        if (m_layoutOsdPhysScreens.value(id) == screen) {
            destroyLayoutOsdWindow(id);
        }
    }

    // Remove navigation OSD windows for this physical screen
    const QStringList navOsdIds = m_navigationOsdWindows.keys();
    for (const QString& id : navOsdIds) {
        if (m_navigationOsdPhysScreens.value(id) == screen) {
            destroyNavigationOsdWindow(id);
        }
    }

    // Clean up snap assist and layout picker if on this physical screen
    if (m_snapAssistScreen == screen) {
        destroySnapAssistWindow();
    }
    if (m_layoutPickerScreen == screen) {
        destroyLayoutPickerWindow();
    }
}

void OverlayService::handleScreenRemoved(QScreen* screen)
{
    destroyAllWindowsForPhysicalScreen(screen);
}

QVariantList OverlayService::buildLayoutsList(const QString& screenId) const
{
    // Determine filter per-screen: check this screen's assignment to decide
    // whether to show manual layouts, autotile algorithms, or both.
    bool includeManual = m_includeManualLayouts;
    bool includeAutotile = m_includeAutotileLayouts;
    auto* layoutManager = dynamic_cast<LayoutManager*>(m_layoutManager);
    if (layoutManager) {
        const QString resolvedId = Utils::isConnectorName(screenId) ? Utils::screenIdForName(screenId) : screenId;
        if (!resolvedId.isEmpty()) {
            const QString assignmentId =
                layoutManager->assignmentIdForScreen(resolvedId, m_currentVirtualDesktop, m_currentActivity);
            if (LayoutId::isAutotile(assignmentId)) {
                includeManual = false;
                includeAutotile = true;
            } else {
                includeManual = true;
                includeAutotile = false;
            }
        }
    }
    const auto entries = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, screenId, m_currentVirtualDesktop, m_currentActivity, includeManual, includeAutotile,
        Utils::screenAspectRatio(screenId), m_settings && m_settings->filterLayoutsByAspectRatio(),
        LayoutUtils::buildCustomOrder(m_settings, includeManual, includeAutotile));
    return LayoutUtils::toVariantList(entries);
}

void OverlayService::setLayoutFilter(bool includeManual, bool includeAutotile)
{
    if (m_includeManualLayouts == includeManual && m_includeAutotileLayouts == includeAutotile) {
        return;
    }
    m_includeManualLayouts = includeManual;
    m_includeAutotileLayouts = includeAutotile;
    // Refresh visible zone selector windows with updated layout list
    refreshVisibleWindows();
}

void OverlayService::setExcludedScreens(const QSet<QString>& screenIds)
{
    m_excludedScreens = screenIds;
}

int OverlayService::visibleLayoutCount(const QString& screenId) const
{
    // Ordering doesn't affect count — skip custom order for performance
    const auto entries = LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, screenId, m_currentVirtualDesktop, m_currentActivity, m_includeManualLayouts,
        m_includeAutotileLayouts, Utils::screenAspectRatio(screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio());
    return entries.size();
}

void OverlayService::onPrepareForSleep(bool goingToSleep)
{
    if (goingToSleep) {
        // System going to sleep - nothing to do
        return;
    }

    // System waking up - restart shader timer to avoid large iTimeDelta
    QMutexLocker locker(&m_shaderTimerMutex);
    if (m_visible && m_shaderTimer.isValid()) {
        m_shaderTimer.restart();
        m_lastFrameTime.store(0);
        qCInfo(lcOverlay) << "Shader timer restarted after system resume";
    }
}

void OverlayService::onShaderError(const QString& errorLog)
{
    qCWarning(lcOverlay) << "Shader error during overlay:" << errorLog;
    m_pendingShaderError = errorLog;
    // Don't set m_shaderErrorPending - retry shaders on next show (fix bugs, don't mask)
}

} // namespace PlasmaZones
