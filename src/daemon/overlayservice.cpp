// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "windowthumbnailservice.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>

#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorSurfaces/SurfaceManagerConfig.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../common/layoutpreviewserialize.h"
#include "../core/unifiedlayoutlist.h"
#include "../core/geometryutils.h"
#include <PhosphorScreens/Manager.h>
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

#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorLayer/defaults/PhosphorShellTransport.h>
#include <QQuickItem>
#include "overlayservice/pz_roles.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// Tear down every PhosphorLayer::Surface referenced by a state entry.
// The Surface owns its QQuickWindow; deleteLater cascades into ~Surface
// which unmaps the layer surface and schedules the window for deletion.
// We never touch the QQuickWindow* directly — double-destroying a Surface-
// owned window was the source of UB in a prior revision of this file.
void releaseSurfacesInState(OverlayService::PerScreenOverlayState& state)
{
    QObject::disconnect(state.overlayGeomConnection);
    state.overlayGeomConnection = {};
    if (state.overlaySurface) {
        state.overlaySurface->deleteLater();
    }
    if (state.zoneSelectorSurface) {
        state.zoneSelectorSurface->deleteLater();
    }
    if (state.layoutOsdSurface) {
        state.layoutOsdSurface->deleteLater();
    }
    if (state.navigationOsdSurface) {
        state.navigationOsdSurface->deleteLater();
    }
    state.overlaySurface = nullptr;
    state.zoneSelectorSurface = nullptr;
    state.layoutOsdSurface = nullptr;
    state.navigationOsdSurface = nullptr;
    state.overlayWindow = nullptr;
    state.zoneSelectorWindow = nullptr;
    state.layoutOsdWindow = nullptr;
    state.navigationOsdWindow = nullptr;
    state.overlayPhysScreen = nullptr;
    state.zoneSelectorPhysScreen = nullptr;
    state.layoutOsdPhysScreen = nullptr;
    state.navigationOsdPhysScreen = nullptr;
}

// Release every surface across the state map, then clear it.
void cleanupAllScreenStates(QHash<QString, OverlayService::PerScreenOverlayState>& states)
{
    for (auto& state : states) {
        releaseSurfacesInState(state);
    }
    states.clear();
}

// Release surfaces for state entries whose key starts with @p prefix,
// then erase those entries from the map.
//
// Semantics: prefix is typically `physId + PhosphorIdentity::VirtualScreenId::Separator`, so
// this function matches ONLY virtual-screen entries (`physId/vs:N`) and
// deliberately skips the bare-physId entry (`physId`). Callers that need
// to clean up the bare entry must do so separately — see the
// onVirtualScreensChangedHandler call site where both are explicitly
// cleaned in sequence.
void cleanupVirtualScreenStates(QHash<QString, OverlayService::PerScreenOverlayState>& states, const QString& prefix)
{
    for (auto it = states.begin(); it != states.end();) {
        if (it.key().startsWith(prefix)) {
            releaseSurfacesInState(it.value());
            it = states.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

OverlayService::OverlayService(Phosphor::Screens::ScreenManager* screenManager, ShaderRegistry* shaderRegistry,
                               QObject* parent)
    : IOverlayService(parent)
    , m_screenProvider(std::make_unique<PhosphorLayer::DefaultScreenProvider>())
    , m_transport(std::make_unique<PhosphorLayer::PhosphorShellTransport>())
{
    m_screenManager = screenManager;
    m_shaderRegistry = shaderRegistry;

    m_surfaceFactory = std::make_unique<PhosphorLayer::SurfaceFactory>(PhosphorLayer::SurfaceFactory::Deps{
        m_transport.get(), m_screenProvider.get(), nullptr, QStringLiteral("plasmazones.overlay")});

    m_surfaceManager = std::make_unique<PhosphorSurfaces::SurfaceManager>(PhosphorSurfaces::SurfaceManagerConfig{
        .surfaceFactory = m_surfaceFactory.get(),
        .engineConfigurator =
            [](QQmlEngine& engine) {
                auto* localizedContext = new PzLocalizedContext(&engine);
                engine.rootContext()->setContextObject(localizedContext);
            },
    });

    // Connect to screen changes (with safety check for early initialization)
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);
    } else {
        qCWarning(lcOverlay) << "Overlay: created before QGuiApplication, screen signals not connected";
    }

    // Connect to virtual screen configuration changes
    if (auto* mgr = m_screenManager) {
        auto onVirtualScreensChangedHandler = [this](const QString& physicalScreenId) {
            // Destroy old overlays for this physical screen, recreate with new config
            QScreen* physScreen = Phosphor::Screens::ScreenIdentity::findByIdOrName(physicalScreenId);
            if (!physScreen) {
                // Physical screen removed -- destroy windows and clean up stale virtual screen entries
                const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
                cleanupVirtualScreenStates(m_screenStates, prefix);
                // Also clean up the bare physical-ID entry (no /vs:N suffix) —
                // cleanupVirtualScreenStates only matches entries starting with "physId/",
                // not the bare "physId" key itself.
                destroyOverlayWindow(physicalScreenId);
                destroyZoneSelectorWindow(physicalScreenId);
                destroyLayoutOsdWindow(physicalScreenId);
                destroyNavigationOsdWindow(physicalScreenId);
                m_screenStates.remove(physicalScreenId);
                return;
            }

            // If the new config HAS virtual screens for this physical ID,
            // destroy any overlay window keyed by the bare physical screen ID
            // itself. Virtual screens use prefixed keys; the bare key would be
            // a leftover from the previous (non-virtual) configuration.
            auto* mgr2 = m_screenManager;
            if (mgr2 && mgr2->hasVirtualScreens(physicalScreenId)) {
                destroyOverlayWindow(physicalScreenId);
                destroyZoneSelectorWindow(physicalScreenId);
                destroyLayoutOsdWindow(physicalScreenId);
                destroyNavigationOsdWindow(physicalScreenId);
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

            // Recreate with new virtual screen config if visible. Reuses
            // mgr2 from above — the Phosphor::Screens::ScreenManager singleton doesn't change
            // mid-lambda, so re-querying would just be noise.
            if (isVisible()) {
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

            // Recreate zone selectors for the new virtual screen configuration.
            // Defer to the next event loop pass to allow PhosphorZones::LayoutRegistry to process
            // assignment migrations for the new virtual screen IDs first, ensuring
            // the zone selector shows the correct layout list.
            if (hadZoneSelector) {
                m_zoneSelectorRecreationPending = true;
                QTimer::singleShot(0, this, [this]() {
                    m_zoneSelectorRecreationPending = false;
                    // m_zoneSelectorVisible was set to false above (to allow recreation).
                    // If an external showZoneSelector() ran during the event loop pass between
                    // posting this timer and its execution, it will have set m_zoneSelectorVisible
                    // back to true — in that case we must NOT call showZoneSelector() again
                    // (double-show). The !m_zoneSelectorVisible guard handles exactly this:
                    // false means "no interim show happened, we still need to recreate";
                    // true means "already re-shown, skip".
                    if (!m_zoneSelectorVisible) {
                        showZoneSelector();
                    }
                });
            }
        };
        connect(mgr, &Phosphor::Screens::ScreenManager::virtualScreensChanged, this, onVirtualScreensChangedHandler);
        // Regions-only changes (swap/rotate/boundary-resize) also need the
        // overlay windows destroyed and recreated with the new VS geometry.
        // The handler is heavy but only runs when overlays are visible
        // (active drag), so the cost is bounded.
        connect(mgr, &Phosphor::Screens::ScreenManager::virtualScreenRegionsChanged, this,
                onVirtualScreensChangedHandler);
    }

    // Connect to system sleep/resume via logind to restart shader timer after wake.
    // This prevents large iTimeDelta jumps when system resumes from sleep.
    // Track the connect result so the dtor can disconnect cleanly rather than
    // leaving a dead entry in QDBusConnection's slot table until the session ends.
    m_prepareForSleepConnected = QDBusConnection::systemBus().connect(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1"),
        QStringLiteral("org.freedesktop.login1.Manager"), QStringLiteral("PrepareForSleep"), this,
        SLOT(onPrepareForSleep(bool)));
    if (!m_prepareForSleepConnected) {
        qCDebug(lcOverlay) << "PrepareForSleep D-Bus signal subscription failed (logind not available?) —"
                           << "shader-timer restart on resume will not run";
    }

    // Reset shader error state on construction (fresh start after reboot)
    m_pendingShaderError.clear();

    m_audioProvider = std::make_unique<PhosphorAudio::CavaSpectrumProvider>();
    connect(m_audioProvider.get(), &PhosphorAudio::IAudioSpectrumProvider::spectrumUpdated, this,
            &OverlayService::onAudioSpectrumUpdated);

    // Keep-alive is managed by m_surfaceManager (created in its constructor).
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

    // Mirror the D-Bus PrepareForSleep connect from the constructor. Not
    // strictly required — QDBusConnection checks receiver-alive before
    // dispatch — but leaving a dead entry in the system bus's slot table
    // for the rest of the session is sloppy.
    if (m_prepareForSleepConnected) {
        QDBusConnection::systemBus().disconnect(QStringLiteral("org.freedesktop.login1"),
                                                QStringLiteral("/org/freedesktop/login1"),
                                                QStringLiteral("org.freedesktop.login1.Manager"),
                                                QStringLiteral("PrepareForSleep"), this, SLOT(onPrepareForSleep(bool)));
        m_prepareForSleepConnected = false;
    }

    // Clean up all window types before engine is destroyed. The Surface owns
    // the QQuickWindow, so deleteLater on the Surface cascades into
    // ~Surface → ~Impl → window teardown in the right order. Never destroy
    // the window directly — that races against ~Surface and dereferences a
    // deleted pointer in ~Impl.
    cleanupAllScreenStates(m_screenStates);

    // Singleton surfaces (snap assist, layout picker, shader preview) are
    // QObject children of `this`, so the QObject parent-child system would
    // destroy them AFTER our own destructor body runs — i.e. after the
    // deferred-delete drain below, and potentially after member destructors
    // (m_surfaceManager, m_transport, m_surfaceFactory, m_screenProvider) have run.
    // Schedule their deletion now so the drain loop picks them up in the
    // right order and they don't touch dead factory pointers during their
    // own teardown.
    if (m_snapAssistSurface) {
        m_snapAssistSurface->deleteLater();
        m_snapAssistSurface = nullptr;
    }
    if (m_layoutPickerSurface) {
        m_layoutPickerSurface->deleteLater();
        m_layoutPickerSurface = nullptr;
    }
    if (m_shaderPreviewSurface) {
        m_shaderPreviewSurface->deleteLater();
        m_shaderPreviewSurface = nullptr;
    }

    // Drain deferred-delete events before m_surfaceManager destroys its engine.
    constexpr int kDrainCap = 64;
    QEventLoop drainLoop;
    int passes = 0;
    for (; passes < kDrainCap; ++passes) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (!drainLoop.processEvents(QEventLoop::ExcludeUserInputEvents)) {
            break;
        }
    }
    if (passes == kDrainCap) {
        qCWarning(lcOverlay) << "deferred-delete drain hit safety cap" << kDrainCap
                             << "— a QML teardown chain is deeper than expected";
    }

    // m_surfaceManager destructor destroys keep-alive + engine
}

PhosphorLayer::Surface* OverlayService::createLayerSurface(const QUrl& qmlUrl, QScreen* screen,
                                                           const PhosphorLayer::Role& role, const char* windowType,
                                                           const QVariantMap& windowProperties,
                                                           std::optional<PhosphorLayer::Anchors> anchorsOverride,
                                                           std::optional<QMargins> marginsOverride)
{
    if (!screen) {
        qCWarning(lcOverlay) << "createLayerSurface: screen is null for" << windowType;
        return nullptr;
    }
    if (!m_surfaceFactory) {
        qCWarning(lcOverlay) << "createLayerSurface: SurfaceFactory not initialised";
        return nullptr;
    }

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = role;
    cfg.contentUrl = qmlUrl;
    cfg.screen = screen;
    cfg.sharedEngine = m_surfaceManager->engine();
    cfg.windowProperties = windowProperties;
    cfg.anchorsOverride = anchorsOverride;
    cfg.marginsOverride = marginsOverride;
    cfg.debugName = QString::fromUtf8(windowType);

    auto* surface = m_surfaceFactory->create(std::move(cfg), this);
    if (!surface) {
        qCWarning(lcOverlay) << "createLayerSurface: factory returned nullptr for" << windowType;
        return nullptr;
    }
    // Warm immediately so the QQuickWindow exists before the caller starts
    // setting properties on it. Mirrors the old createQmlWindow+configureLayerSurface
    // sequence where the window was live before return.
    surface->warmUp();
    const auto st = surface->state();
    if (st == PhosphorLayer::Surface::State::Failed) {
        qCWarning(lcOverlay) << "createLayerSurface: warmUp failed for" << windowType;
        surface->deleteLater();
        return nullptr;
    }
    if (st == PhosphorLayer::Surface::State::Warming) {
        // QML is still loading asynchronously. Every consumer in this file
        // uses qrc:/ URLs which load synchronously, so Warming-on-return
        // means someone introduced an async source path (remote URL, QML
        // bytecode fetched from file with background imports, etc.) without
        // updating the caller to listen for Surface::stateChanged. The
        // callers below assume a live QQuickWindow and deref surface->window()
        // synchronously — that would segfault. Refuse up-front so the bug
        // manifests as a clean refusal instead of a crash.
        qCWarning(lcOverlay)
            << "createLayerSurface: surface still Warming on return for" << windowType
            << "— async QML load path is not supported by the current callers. Switch to qrc:/ or refactor"
            << "the caller to wait on Surface::stateChanged before touching surface->window().";
        surface->deleteLater();
        return nullptr;
    }

    // Pipeline cache: mirror createQmlWindow's behaviour so shader compilation
    // persists across daemon restarts.
    if (auto* w = surface->window()) {
        const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        if (!cacheDir.isEmpty()) {
            static bool s_cacheDirCreated = false;
            if (!s_cacheDirCreated) {
                QDir().mkpath(cacheDir);
                s_cacheDirCreated = true;
            }
            QQuickGraphicsConfiguration config = w->graphicsConfiguration();
            config.setPipelineCacheSaveFile(cacheDir + QStringLiteral("/plasmazones-pipeline.cache"));
            w->setGraphicsConfiguration(config);
        }
#if QT_CONFIG(vulkan)
        auto* vulkanInstance = qApp->property(PlasmaZones::PzVulkanInstanceProperty).value<QVulkanInstance*>();
        if (!vulkanInstance && QQuickWindow::graphicsApi() == QSGRendererInterface::Vulkan) {
            if (!m_fallbackVulkanInstance) {
                m_fallbackVulkanInstance = std::make_unique<QVulkanInstance>();
                m_fallbackVulkanInstance->setApiVersion(PlasmaZones::PzVulkanApiVersion);
                if (m_fallbackVulkanInstance->create()) {
                    qApp->setProperty(PlasmaZones::PzVulkanInstanceProperty,
                                      QVariant::fromValue(m_fallbackVulkanInstance.get()));
                    vulkanInstance = m_fallbackVulkanInstance.get();
                } else {
                    qCCritical(lcOverlay) << "Failed to create fallback QVulkanInstance for" << windowType;
                    m_fallbackVulkanInstance.reset();
                }
            }
        }
        if (vulkanInstance) {
            w->setVulkanInstance(vulkanInstance);
        }
#endif
    }

    return surface;
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
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QCursor::pos(), cursorScreen);
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
            QString effectiveId = Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
            if (isContextDisabled(m_settings, effectiveId, m_currentVirtualDesktop, m_currentActivity)) {
                return;
            }
        }
    }

    const QPoint cursorPos(cursorX, cursorY);

    if (m_visible) {
        // One-overlay-per-VS architecture: every VS already has a live
        // overlay window from initializeOverlay. Cross-VS switching is
        // just a matter of flipping per-window _idled state — no
        // re-init, no rekey, no layer-shell re-anchor. This sidesteps
        // the earlier "wrong spot" bug where rekey moved the map entry
        // but left the layer surface anchored to the previous VS's
        // bounds, and the full NVIDIA vkDestroyDevice deadlock on any
        // destroy path.
        if (!cursorScreen) {
            cursorScreen = Utils::findScreenAtPosition(cursorPos);
        }
        if (!cursorScreen) {
            return;
        }
        const QString cursorEffectiveId =
            Utils::effectiveScreenIdAt(m_screenManager, QPoint(cursorX, cursorY), cursorScreen);
        if (cursorEffectiveId.isEmpty()) {
            return;
        }
        m_currentOverlayScreenId = showOnAllMonitors ? QString() : cursorEffectiveId;
        applyIdleStateForCursor(cursorEffectiveId, showOnAllMonitors);
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
    // initializeOverlay() will call createOverlayWindow() since overlay windows
    // are now destroyed.
    const QStringList screenIds = m_screenStates.keys();
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

void OverlayService::setIdleForDragPause()
{
    // Blank the overlay's shader output without destroying QQuickWindows.
    // The heavy hide() path pays a ~QQuickWindow Vulkan teardown per screen
    // which blocks the main thread on the scene graph render thread — and
    // with modifier-key thrashing during a drag we ended up paying that cost
    // many times per second, stalling D-Bus dispatch long enough for
    // kwin-effect's endDrag to time out and the user to see multi-second lag.
    //
    // Here we only clear the per-window QML properties that drive the shader
    // (zones, zoneCount, highlights). Windows, Vulkan swap chains, and layer
    // surfaces stay alive. On the next activation tick, refreshFromIdle()
    // re-pushes the current zone data — cheap because the labels-texture
    // build is hash-cached on unchanged inputs.
    if (!m_visible) {
        return;
    }
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        QQuickWindow* window = it.value().overlayWindow;
        if (!window) {
            continue;
        }
        // _idled gates content.visible and toggles Qt.WindowTransparentForInput
        // in the overlay QML (RenderNodeOverlay.qml / ZoneOverlay.qml). That
        // makes the wl_surface effectively invisible and non-input-absorbing
        // in place, without destroying the QQuickWindow. Blanking only the
        // zones properties below is not sufficient — on some shaders the
        // base pass still renders visible output when zoneCount==0, and the
        // input region stays active until the flag change lands.
        writeQmlProperty(window, QStringLiteral("_idled"), true);
        writeQmlProperty(window, QStringLiteral("zones"), QVariantList());
        writeQmlProperty(window, QStringLiteral("zoneCount"), 0);
        writeQmlProperty(window, QStringLiteral("highlightedCount"), 0);
        writeQmlProperty(window, QStringLiteral("highlightedZoneId"), QString());
        writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), QVariantList());
        // NOTE: labelsTextureHash is intentionally NOT cleared here. The QML
        // side's labelsTexture property still holds the previously-built image
        // (setProperty was never called with a new one); it just isn't sampled
        // while zoneCount is 0. Keeping the hash means refreshFromIdle() with
        // unchanged zones hits the cache and costs one hash compute instead
        // of rebuilding 23 MB of pixels.
    }
    // CRITICAL: mark zone data CLEAN, not dirty. The shader animation tick
    // (shader.cpp:245) re-runs updateZonesForAllWindows() whenever dirty is
    // set, which would rebuild the real zones and undo the blank. The idle
    // state is "what we just wrote, do not re-derive from layout data until
    // refreshFromIdle() is called."
    m_zoneDataDirty = false;
    // NOTE: we deliberately do NOT call stopShaderAnimation() here. The
    // shader timer keeps ticking at ~60 Hz while idled, but with zoneCount
    // set to 0 the per-frame work collapses to a handful of uniform uploads
    // to a surface that's rendering no visible geometry — bounded cost, O(1)
    // per screen. Pausing and restarting the timer across the idle cycle
    // would require additional state tracking in refreshFromIdle() and add
    // a startup transient on every modifier re-press. Left unchanged for
    // simplicity; revisit if profiling ever shows it as a hot spot.
}

void OverlayService::refreshFromIdle()
{
    // Restore zone data after a setIdleForDragPause() blank and flip
    // the active VS's overlay back to visible.
    //
    // setIdleForDragPause() unconditionally idles every overlay (zones
    // blanked + _idled=true), so refreshFromIdle() re-pushes zone data
    // to all of them and then applies the cursor-based idle state to
    // un-idle the one the cursor is currently on. The L2 labels-texture
    // hash cache keeps the shader-path re-push cheap on unchanged inputs.
    if (!m_visible) {
        return;
    }
    updateZonesForAllWindows();
    // Resolve the cursor's current VS — the drag adaptor keeps
    // m_currentOverlayScreenId updated via showAtPosition, so this
    // reflects the last VS the cursor was observed on.
    const bool showOnAllMonitors = !m_settings || m_settings->showZonesOnAllMonitors();
    applyIdleStateForCursor(m_currentOverlayScreenId, showOnAllMonitors);
}

void OverlayService::applyIdleStateForCursor(const QString& activeEffectiveId, bool showOnAllMonitors)
{
    // One-overlay-per-VS idle state: iterate every live overlay window
    // and flip its _idled QML property based on whether its VS should
    // currently be accepting input / rendering content.
    //
    // - showOnAllMonitors=true  → all overlays un-idled (all VSes active)
    // - showOnAllMonitors=false → only activeEffectiveId un-idled
    // - activeEffectiveId empty → all overlays idled (no active VS —
    //   used by setIdleForDragPause when drag-end hasn't chosen a next
    //   cursor position yet, or when the cursor sits on a disabled VS)
    //
    // The write is idempotent: QML property binding only re-evaluates
    // when the value actually changes, so flipping _idled on a window
    // that's already in the target state is free.
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        QQuickWindow* window = it.value().overlayWindow;
        if (!window) {
            continue;
        }
        const bool shouldBeActive =
            showOnAllMonitors || (it.key() == activeEffectiveId && !activeEffectiveId.isEmpty());
        writeQmlProperty(window, QStringLiteral("_idled"), !shouldBeActive);
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

void OverlayService::setLayout(PhosphorZones::Layout* layout)
{
    if (m_layout != layout) {
        m_layout = layout;
        // Mark zone data as dirty when layout changes to ensure shader overlay updates
        m_zoneDataDirty = true;
    }
}

PhosphorZones::Layout* OverlayService::resolveScreenLayout(QScreen* screen) const
{
    // Physical QScreen* overload: derives screenId and delegates.
    // Callers with a known virtual screenId should use the QString overload directly.
    if (!screen) {
        return m_layout;
    }
    return resolveScreenLayout(Phosphor::Screens::ScreenIdentity::identifierFor(screen));
}

PhosphorZones::Layout* OverlayService::resolveScreenLayout(const QString& screenId) const
{
    PhosphorZones::Layout* screenLayout = nullptr;
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
        const QStringList screenIds = m_screenStates.keys();
        for (const QString& screenId : screenIds) {
            if (isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
                destroyZoneSelectorWindow(screenId);
                if (m_visible) {
                    destroyOverlayWindow(screenId);
                }
            }
        }
    }

    // Update remaining (non-disabled) zone selector and overlay windows
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        const QString& screenId = it.key();
        if (isContextDisabled(m_settings, screenId, m_currentVirtualDesktop, m_currentActivity)) {
            continue;
        }
        if (it.value().zoneSelectorWindow) {
            updateZoneSelectorWindow(screenId);
        }
        if (m_visible && it.value().overlayWindow && it.value().overlayPhysScreen) {
            updateOverlayWindow(screenId, it.value().overlayPhysScreen);
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
    auto* mgr = m_screenManager;
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (mgr && mgr->hasVirtualScreens(physId)) {
        for (const QString& vsId : mgr->virtualScreenIdsFor(physId)) {
            if (!m_screenStates.contains(vsId) || !m_screenStates[vsId].overlayWindow) {
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
        if (!m_screenStates.contains(physId) || !m_screenStates[physId].overlayWindow) {
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
    const QString physScreenId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);

    auto* mgr = m_screenManager;
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
                if (auto* window = m_screenStates.value(vsId).overlayWindow) {
                    assertWindowOnScreen(window, screen, vsGeom);
                    window->show();
                }
            }
        }
    } else {
        createOverlayWindow(screen);
        updateOverlayWindow(screen);
        if (auto* window = m_screenStates.value(physScreenId).overlayWindow) {
            assertWindowOnScreen(window, screen);
            window->show();
        }
    }
}

void OverlayService::destroyAllWindowsForPhysicalScreen(QScreen* screen)
{
    // Remove all windows associated with this physical screen
    // (includes any virtual screens on this physical screen)
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& id : screenIds) {
        const auto& state = m_screenStates[id];
        if (state.overlayPhysScreen == screen || state.zoneSelectorPhysScreen == screen
            || state.layoutOsdPhysScreen == screen || state.navigationOsdPhysScreen == screen) {
            destroyOverlayWindow(id);
            destroyZoneSelectorWindow(id);
            destroyLayoutOsdWindow(id);
            destroyNavigationOsdWindow(id);
            // If every window for this screen-id was already released (or
            // this state entry never actually held any — e.g. an OSD
            // creation failed earlier), drop the empty shell so screen
            // hot-plug cycles don't slowly accumulate dead keys. Matches
            // cleanupVirtualScreenStates semantics: the state entry is
            // meaningless without at least one live window.
            auto& s = m_screenStates[id];
            if (!s.overlaySurface && !s.zoneSelectorSurface && !s.layoutOsdSurface && !s.navigationOsdSurface) {
                m_screenStates.remove(id);
            }
        }
    }

    // Clean up snap assist and layout picker if on this physical screen
    if (m_snapAssistScreen == screen) {
        destroySnapAssistWindow();
    }
    if (m_layoutPickerScreen == screen) {
        destroyLayoutPickerWindow();
    }

    // Drop navigation-OSD "creation failed" sentinels for screen ids rooted
    // on this physical screen. Without this, if the same physical monitor
    // is reconnected (hot-plug cycle) it inherits the stale flag and we
    // silently refuse to recreate the OSD. Matching is prefix-based because
    // virtual-screen ids embed the physical id as the prefix.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (!physId.isEmpty()) {
        for (auto it = m_navigationOsdCreationFailed.begin(); it != m_navigationOsdCreationFailed.end();) {
            if (it.key() == physId || it.key().startsWith(physId + PhosphorIdentity::VirtualScreenId::Separator)) {
                it = m_navigationOsdCreationFailed.erase(it);
            } else {
                ++it;
            }
        }
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
    if (m_layoutManager) {
        const QString resolvedId = Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)
            ? Phosphor::Screens::ScreenIdentity::idForName(screenId)
            : screenId;
        if (!resolvedId.isEmpty()) {
            const QString assignmentId =
                m_layoutManager->assignmentIdForScreen(resolvedId, m_currentVirtualDesktop, m_currentActivity);
            if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
                includeManual = false;
                includeAutotile = true;
            } else {
                includeManual = true;
                includeAutotile = false;
            }
        }
    }
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, screenId, m_currentVirtualDesktop, m_currentActivity, includeManual,
        includeAutotile, Utils::screenAspectRatio(m_screenManager, screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio(),
        PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, includeManual, includeAutotile),
        m_autotileLayoutSource);
    return PlasmaZones::toVariantList(entries);
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
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, screenId, m_currentVirtualDesktop, m_currentActivity,
        m_includeManualLayouts, m_includeAutotileLayouts, Utils::screenAspectRatio(m_screenManager, screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio(),
        /*customOrder=*/{}, m_autotileLayoutSource);
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
