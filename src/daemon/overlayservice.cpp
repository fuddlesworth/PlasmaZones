// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "snapassistthumbnailprovider.h"

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
#include <QGuiApplication>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QMutexLocker>

#include "../core/logging.h"
#include "pz_qml_i18n.h"
#include "vulkan_support.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/SurfaceAnimator.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorLayer/Role.h>
#include <PhosphorLayer/Surface.h>
#include <PhosphorLayer/SurfaceConfig.h>
#include <PhosphorLayer/SurfaceFactory.h>
#include <PhosphorLayer/defaults/DefaultScreenProvider.h>
#include <PhosphorLayer/defaults/PhosphorWaylandTransport.h>
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
    if (state.notificationSurface) {
        state.notificationSurface->deleteLater();
    }
    state.overlaySurface = nullptr;
    state.zoneSelectorSurface = nullptr;
    state.notificationSurface = nullptr;
    state.overlayWindow = nullptr;
    state.zoneSelectorWindow = nullptr;
    state.notificationWindow = nullptr;
    state.overlayPhysScreen = nullptr;
    state.zoneSelectorPhysScreen = nullptr;
    state.notificationPhysScreen = nullptr;
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

// Per-role SurfaceAnimator config builders + setupSurfaceAnimator +
// applyShaderProfilesToAnimator are extracted to
// overlayservice/animation_config.cpp to keep this translation unit
// under the project's <800-line guideline.

void OverlayService::primeSurfaceRenderPipeline(PhosphorLayer::Surface* surface)
{
    if (!surface) {
        return;
    }
    // contains-check covers BOTH lifecycle stages of a single prime
    // (pending warm + window-armed): once a surface is in the set, no
    // path adds another stateChanged or frameSwapped lambda to it.
    // Without this gate, an external double-call to
    // primeSurfaceRenderPipeline (e.g. show path that races a screen
    // reconfigure) would arm a second frameSwapped connection — one
    // would fire and hide the surface mid-content.
    if (m_primingSurfaces.contains(surface)) {
        return;
    }

    // Single destroyed-cleanup per surface (per OverlayService
    // instance), tracked in m_primingDestroyedConnections. Replaces
    // the earlier `pz_primingDestroyedConnected` dynamic-property
    // gate which leaked across service instances — a fresh service
    // re-encountering the same Surface* would skip wiring its own
    // cleanup. The slot's static_cast on `dying` is safe because the
    // resulting pointer is only used as a hash-map key (compare-by-
    // address); ~QObject has already run by the time destroyed fires.
    if (!m_primingDestroyedConnections.contains(surface)) {
        QMetaObject::Connection destroyedConn = connect(surface, &QObject::destroyed, this, [this](QObject* dying) {
            auto* surf = static_cast<PhosphorLayer::Surface*>(dying);
            m_primingSurfaces.remove(surf);
            m_primingFrameConnections.remove(surf);
            m_primingDestroyedConnections.remove(surf);
        });
        m_primingDestroyedConnections.insert(surface, destroyedConn);
    }

    auto* window = surface->window();
    if (!window) {
        // Surface hasn't materialised a QQuickWindow yet — Surface::warmUp
        // is asynchronous for content that compiles off the main thread.
        // Defer until warm completes (stateChanged Warming → Hidden).
        // Disconnect on the FIRST Hidden — even if the window is somehow
        // still null we drop the connection rather than letting it stay
        // armed forever and re-fire on every later state change. The
        // recursive call lands in the window-non-null branch which adds
        // to m_primingSurfaces and installs the frameSwapped path.
        //
        // Insert into m_primingSurfaces NOW (warm-pending sentinel) so
        // an external second call to primeSurfaceRenderPipeline before
        // the first warm completes hits the contains() guard above and
        // bails — without this, the second call would queue a SECOND
        // stateChanged lambda whose recursive call lands in the window-
        // path's contains() bail at line `m_primingSurfaces.contains
        // (surface) → return` after the first one already inserted +
        // armed, leaking the second stateChanged slot for the rest of
        // the surface's lifetime.
        //
        // Disconnects on Hidden OR Failed: a surface stuck in Failed
        // never reaches Hidden, so without the Failed branch the
        // sentinel sits in m_primingSurfaces indefinitely, blocking
        // any future re-prime even after a recovery path puts the
        // surface back into a usable state.
        m_primingSurfaces.insert(surface);
        QPointer<PhosphorLayer::Surface> guard(surface);
        auto warmConn = std::make_shared<QMetaObject::Connection>();
        *warmConn = connect(surface, &PhosphorLayer::Surface::stateChanged, this,
                            [this, guard, warmConn](PhosphorLayer::Surface::State newState) {
                                if (newState != PhosphorLayer::Surface::State::Hidden
                                    && newState != PhosphorLayer::Surface::State::Failed) {
                                    return;
                                }
                                QObject::disconnect(*warmConn);
                                if (!guard) {
                                    return;
                                }
                                // Drop the warm-pending sentinel BEFORE the
                                // recursive call so the window-path's
                                // contains() guard re-evaluates to false
                                // and proceeds to insert + arm the
                                // frameSwapped handler.
                                //
                                // CRITICAL: gate the recursive prime on
                                // `m_primingSurfaces.remove(...)` returning
                                // true. If a user-show called
                                // cancelSurfacePrime during the warm-pending
                                // window, the surface was already removed
                                // from the set, `remove()` returns false,
                                // and we MUST NOT recurse — recursion would
                                // re-arm a fresh prime cycle whose
                                // frameSwapped-driven hide() races the
                                // user's just-shown content off the screen.
                                // Without this guard, cancelSurfacePrime is
                                // a silent no-op while the warm hasn't
                                // completed.
                                const bool stillPriming = m_primingSurfaces.remove(guard.data());
                                if (stillPriming && newState == PhosphorLayer::Surface::State::Hidden
                                    && guard->window() != nullptr) {
                                    primeSurfaceRenderPipeline(guard.data());
                                }
                            });
        return;
    }
    m_primingSurfaces.insert(surface);

    // Hide on the first frameSwapped after surface->show(). By that
    // point the wl_surface is mapped, the Vulkan swapchain has at
    // least one image, and the QML scene-graph (including any
    // QSGLayer that the shader path will later use) has rendered
    // at least one frame.
    //
    // The connection is tracked in m_primingFrameConnections so
    // cancelSurfacePrime can disconnect it explicitly — without
    // tracking, the connection survives until next paint and we
    // accumulate one stale slot per prime cycle for the surface's
    // lifetime under rapid show/hide.
    QPointer<PhosphorLayer::Surface> guard(surface);
    QMetaObject::Connection frameConn = connect(window, &QQuickWindow::frameSwapped, this, [this, guard]() {
        if (!guard) {
            // Surface died after the connection was armed but before
            // first frameSwapped — the destroyed-signal lambda in
            // m_primingDestroyedConnections has already cleaned the
            // map entry, and Qt's sender-destruction auto-disconnect
            // (window dies with surface) will retire this lambda
            // shortly. Nothing to do here.
            return;
        }
        const auto connIt = m_primingFrameConnections.find(guard.data());
        if (connIt != m_primingFrameConnections.end()) {
            QObject::disconnect(connIt.value());
            m_primingFrameConnections.erase(connIt);
        }
        // Only hide if the user hasn't already taken over the surface
        // (cancelSurfacePrime would have removed us from the set).
        if (m_primingSurfaces.remove(guard.data())) {
            guard->hide();
        }
    });
    m_primingFrameConnections.insert(surface, frameConn);
    surface->show();
}

void OverlayService::cancelSurfacePrime(PhosphorLayer::Surface* surface)
{
    // Idempotent — called from every user show path so a non-priming
    // surface short-circuits cheaply. Disconnect the frameSwapped
    // lambda EXPLICITLY (tracked in m_primingFrameConnections) so the
    // queued hide-on-first-paint never fires after a user-show. The
    // m_primingSurfaces.remove() is the secondary guard the lambda
    // would also check, but explicit disconnection is the safer
    // primary contract — any future event-loop pump between cancel
    // and the user's surface->show() is now harmless. Surfaces that
    // get torn down outside of cancelSurfacePrime are cleaned via the
    // destroyed signal connection in m_primingDestroyedConnections.
    m_primingSurfaces.remove(surface);
    const auto connIt = m_primingFrameConnections.find(surface);
    if (connIt != m_primingFrameConnections.end()) {
        QObject::disconnect(connIt.value());
        m_primingFrameConnections.erase(connIt);
    }
}

OverlayService::OverlayService(Phosphor::Screens::ScreenManager* screenManager, ShaderRegistry* shaderRegistry,
                               PhosphorAnimation::PhosphorProfileRegistry* profileRegistry, QObject* parent)
    : IOverlayService(parent)
    , m_screenProvider(std::make_unique<PhosphorLayer::DefaultScreenProvider>())
    , m_transport(std::make_unique<PhosphorLayer::PhosphorWaylandTransport>())
{
    m_screenManager = screenManager;
    m_shaderRegistry = shaderRegistry;

    // The profile registry is non-optional: SurfaceAnimator binds to it by
    // reference. Composition roots own a single PhosphorProfileRegistry
    // and thread it through every consumer — fail loud if the wiring is
    // wrong rather than silently falling back to library defaults.
    Q_ASSERT_X(profileRegistry, "OverlayService::OverlayService",
               "profileRegistry must not be null: composition root must own and inject the registry");

    // Phase-5 SurfaceAnimator. One instance drives every overlay's
    // show/hide via Profile-resolved curves; per-Role configs install
    // below in setupSurfaceAnimator(). Constructed BEFORE the
    // SurfaceFactory because the factory's Deps captures the animator
    // pointer; Surfaces produced after this point dispatch through it
    // on every show/hide.
    setupSurfaceAnimator(*profileRegistry);

    m_surfaceFactory = std::make_unique<PhosphorLayer::SurfaceFactory>(
        PhosphorLayer::SurfaceFactory::Deps{.transport = m_transport.get(),
                                            .screens = m_screenProvider.get(),
                                            .engineProvider = nullptr,
                                            .animator = m_surfaceAnimator.get(),
                                            .loggingCategory = QStringLiteral("plasmazones.overlay")});

    const QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString pipelineCachePath =
        cacheDir.isEmpty() ? QString() : (cacheDir + QStringLiteral("/plasmazones-pipeline.cache"));

    QVulkanInstance* externalVulkanInstance = nullptr;
#if QT_CONFIG(vulkan)
    externalVulkanInstance = qApp->property(PlasmaZones::PzVulkanInstanceProperty).value<QVulkanInstance*>();
#endif

    // Construct the thumbnail provider eagerly so the borrowed @c m_thumbnailProvider
    // pointer is non-null from this point onwards. The SurfaceManager (and
    // its engine) is created next; the engineConfigurator releases ownership
    // to the engine once it exists. Until then the unique_ptr keeps the
    // provider alive — there is no longer any window where a D-Bus
    // setSnapAssistThumbnail call would silently drop because the engine
    // hasn't materialised yet.
    m_thumbnailProviderOwned = std::make_unique<SnapAssistThumbnailProvider>();
    m_thumbnailProvider.store(m_thumbnailProviderOwned.get(), std::memory_order_release);

    m_surfaceManager = std::make_unique<PhosphorSurfaces::SurfaceManager>(PhosphorSurfaces::SurfaceManagerConfig{
        .surfaceFactory = m_surfaceFactory.get(),
        .engineConfigurator =
            [this](QQmlEngine& engine) {
                auto* localizedContext = new PzLocalizedContext(&engine);
                engine.rootContext()->setContextObject(localizedContext);
                engine.rootContext()->setContextProperty(QStringLiteral("overlayService"), this);

                // Bounded LRU cache + image provider for Snap Assist thumbnails.
                // QQmlEngine::addImageProvider takes ownership; transfer the
                // already-live provider out of the unique_ptr so the engine
                // becomes the sole owner. The borrowed @c m_thumbnailProvider
                // raw pointer remains valid for the engine's lifetime, which
                // outlives every QML element it spawns, so QML callbacks
                // that hit requestImage are safe.
                //
                // The engine's @c destroyed signal nulls @c m_thumbnailProvider
                // before any subsequent D-Bus dispatch can dereference it.
                // Without this hook, late @c setSnapAssistThumbnail traffic
                // arriving after the engine is gone (e.g. forced
                // SurfaceManager teardown outside @c ~OverlayService) would
                // see a dangling raw pointer.
                //
                // Re-entrancy: if @c engineConfigurator is ever invoked again
                // (a future SurfaceManager that recreates its engine), the
                // unique_ptr will be empty after the first @c release(). Mint
                // a fresh provider so the second engine isn't quietly
                // unregistered from snap-assist thumbnails. Today the engine
                // is single-instance for the daemon's lifetime, but defending
                // here costs ~3 lines and removes a foot-gun if that
                // invariant ever changes.
                if (!m_thumbnailProviderOwned) {
                    m_thumbnailProviderOwned = std::make_unique<SnapAssistThumbnailProvider>();
                    m_thumbnailProvider.store(m_thumbnailProviderOwned.get(), std::memory_order_release);
                }
                engine.addImageProvider(QString::fromLatin1(SnapAssistThumbnailProvider::ProviderId),
                                        m_thumbnailProviderOwned.release());
                QObject::connect(&engine, &QObject::destroyed, this, [this]() {
                    m_thumbnailProvider.store(nullptr, std::memory_order_release);
                });
            },
        .pipelineCachePath = pipelineCachePath,
        .vulkanInstance = externalVulkanInstance,
        .vulkanApiVersion = PlasmaZones::PzVulkanApiVersion,
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
                destroyNotificationWindow(physicalScreenId);
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
                destroyNotificationWindow(physicalScreenId);
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
        qCDebug(lcOverlay) << "PrepareForSleep D-Bus signal subscription failed (logind not available?):"
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
    // Disconnect from QGuiApplication first so we don't get screen-related callbacks
    // while we're destroying windows.
    if (qGuiApp) {
        disconnect(qGuiApp, nullptr, this, nullptr);
    }

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
    // member destructors. Schedule their deletion now so SurfaceManager's
    // drain loop picks them up before the engine is destroyed.
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

    // Drain deferred-delete events NOW, while all OverlayService members are
    // still alive. Surface destructors may touch m_screenStates, m_shaderRegistry,
    // etc. — if we let ~m_surfaceManager's drain run instead, those members could
    // already be destroyed (C++ member destruction order is reverse declaration).
    m_surfaceManager->drainDeferredDeletes();

    // Explicitly disconnect + clear the prime-tracking maps so the
    // invariant ("every Connection retired before its sender's window
    // is gone") doesn't depend on Qt's receiver-context auto-disconnect
    // ordering during member destruction. After drainDeferredDeletes
    // every prime-tracked surface is destroyed, so most Connections are
    // already retired by sender-destruction; this loop is defensive
    // against any future path that adds prime-tracked surfaces outside
    // of m_screenStates / the three explicit singletons.
    for (const auto& conn : std::as_const(m_primingFrameConnections)) {
        QObject::disconnect(conn);
    }
    m_primingFrameConnections.clear();
    for (const auto& conn : std::as_const(m_primingDestroyedConnections)) {
        QObject::disconnect(conn);
    }
    m_primingDestroyedConnections.clear();
    m_primingSurfaces.clear();
}

PhosphorLayer::Surface* OverlayService::createLayerSurface(LayerSurfaceParams params)
{
    if (!params.screen) {
        qCWarning(lcOverlay) << "createLayerSurface: screen is null for" << params.windowType;
        return nullptr;
    }

    PhosphorLayer::SurfaceConfig cfg;
    cfg.role = std::move(params.role);
    cfg.contentUrl = std::move(params.qmlUrl);
    cfg.screen = params.screen;
    cfg.windowProperties = std::move(params.windowProperties);
    cfg.anchorsOverride = std::move(params.anchorsOverride);
    cfg.marginsOverride = std::move(params.marginsOverride);
    cfg.keepMappedOnHide = params.keepMappedOnHide;
    // SurfaceConfig::initialSize uses isEmpty() as the "unset" sentinel —
    // forwarding the param verbatim preserves that contract (empty here →
    // empty there → fall back to screen geometry inside surface.cpp).
    cfg.initialSize = params.initialSize;
    cfg.debugName = QString::fromUtf8(params.windowType);

    return m_surfaceManager->createSurface(std::move(cfg), this);
}

PhosphorLayer::Surface* OverlayService::createWarmedOsdSurface(const PhosphorLayer::Role& role, const QUrl& qmlUrl,
                                                               QScreen* physScreen, const char* windowType,
                                                               const QString& screenId)
{
    // OSD surfaces are screen-sized (mirrors snap-assist / zone-selector).
    // Phase prior to this change kept OSD wl_surfaces content-sized (240×70
    // toast) and the layer-shell margins did the on-screen centering, but
    // that left vertex-shader transitions like fly-in clipped at the
    // surface edge — geometry shifted past the surface bounds is dropped
    // by the compositor. A screen-sized OSD surface gives shader effects
    // headroom equal to the screen, and keeps the wiring path identical
    // to popups (which were already screen-sized) so a single
    // `boundsExtent` mechanism works uniformly across every overlay role.
    //
    // Cost is real but bearable: a fullscreen swapchain runs ~25 MB at 4K
    // on the NVIDIA proprietary stack, vs ~tens of KB for the content-
    // sized warm-up. With one notification surface per effective screen
    // (~1–6 in typical setups), that's ~25–150 MB. Damage tracking keeps
    // the per-frame cost negligible while idle: a fullscreen surface with
    // a small centred card only repaints the card region.
    QRect screenGeom;
    if (!screenId.isEmpty() && m_screenManager) {
        screenGeom = m_screenManager->screenGeometry(screenId);
    }
    if (!screenGeom.isValid() && physScreen) {
        screenGeom = physScreen->geometry();
    }
    QSize initialSize = screenGeom.isValid() ? screenGeom.size() : QSize(240, 70);

    // Virtual-screen-aware anchors / margins, same vocabulary popups use
    // (see selector.cpp::createZoneSelectorWindow). Physical screen →
    // AnchorAll + zero margins so the compositor sizes the surface to the
    // full output. Virtual screen → Top|Left + offset margins pinning the
    // surface to the VS sub-rect's top-left within its physical screen.
    std::optional<PhosphorLayer::Anchors> anchorsOverride;
    std::optional<QMargins> marginsOverride;
    if (physScreen && screenGeom.isValid()) {
        const bool isVS = !screenId.isEmpty() && PhosphorIdentity::VirtualScreenId::isVirtual(screenId);
        const auto placement = layerPlacementForVs(isVS ? screenGeom : QRect(), physScreen->geometry());
        anchorsOverride = placement.anchors;
        if (!placement.margins.isNull()) {
            marginsOverride = placement.margins;
        }
    }

    auto* surface = createLayerSurface({.qmlUrl = qmlUrl,
                                        .screen = physScreen,
                                        .role = role,
                                        .windowType = windowType,
                                        .anchorsOverride = anchorsOverride,
                                        .marginsOverride = marginsOverride,
                                        .keepMappedOnHide = true,
                                        .initialSize = initialSize});
    if (!surface) {
        return nullptr;
    }

    // Wire the QML-side auto-dismiss signal to Surface::hide(). The OSD
    // content components (LayoutOsdContent, NavigationOsdContent) both
    // expose `signal dismissRequested()` driven by their shared
    // OsdDismissable timer; the unified NotificationOverlay host
    // re-emits each loaded content's signal as its own dismissRequested.
    // LayoutPickerOverlay uses the same name (post-#9 rename) for
    // backdrop-click dismissal. String-based connect is the only path
    // because QML-defined signals aren't addressable via Qt5
    // `&Class::signal` pointers.
    if (auto* window = surface->window()) {
        QObject::connect(window, SIGNAL(dismissRequested()), surface, SLOT(hide()));
    }
    return surface;
}

// Overlay show/hide/toggle + setIdleForDragPause/refreshFromIdle/
// applyIdleStateForCursor are extracted to overlayservice/lifecycle.cpp
// alongside the existing selector/snapassist/osd splits.

void OverlayService::setAnimationShaderRegistry(PhosphorAnimationShaders::AnimationShaderRegistry* registry)
{
    m_animShaderRegistry = registry;
    if (m_surfaceAnimator) {
        m_surfaceAnimator->setAnimationShaderRegistry(registry);
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
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
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
        if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId, m_currentVirtualDesktop,
                              m_currentActivity)) {
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
                                         << ", skipping overlay creation";
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
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, vsId, m_currentVirtualDesktop,
                                  m_currentActivity)) {
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
            || state.notificationPhysScreen == screen) {
            destroyOverlayWindow(id);
            destroyZoneSelectorWindow(id);
            destroyNotificationWindow(id);
            // If every window for this screen-id was already released (or
            // this state entry never actually held any — e.g. an OSD
            // creation failed earlier), drop the empty shell so screen
            // hot-plug cycles don't slowly accumulate dead keys. Matches
            // cleanupVirtualScreenStates semantics: the state entry is
            // meaningless without at least one live window.
            auto& s = m_screenStates[id];
            if (!s.overlaySurface && !s.zoneSelectorSurface && !s.notificationSurface) {
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

    // Drop notification-window "creation failed" sentinels for screen ids
    // rooted on this physical screen. Without this, if the same physical
    // monitor is reconnected (hot-plug cycle) it inherits the stale flag
    // and we silently refuse to recreate the OSD. Matching is prefix-based
    // because virtual-screen ids embed the physical id as the prefix.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    if (!physId.isEmpty()) {
        const QString vsPrefix = physId + PhosphorIdentity::VirtualScreenId::Separator;
        for (auto it = m_notificationCreationFailed.begin(); it != m_notificationCreationFailed.end();) {
            if (*it == physId || it->startsWith(vsPrefix)) {
                it = m_notificationCreationFailed.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Drop the dedup sentinel for this physical screen so a hot-plug cycle
    // doesn't suppress the first navigation OSD on the reconnected monitor
    // when it lands inside the implicit 200 ms timeout window. The dedup is
    // keyed on the screenId at time-of-fire, and a removed-then-readded
    // monitor reuses the same id — without this clear, a navigation action
    // on the readded screen within 200 ms of the last action on the
    // pre-removal incarnation gets silently swallowed.
    if (!physId.isEmpty()
        && (m_lastNavigationScreenId == physId
            || m_lastNavigationScreenId.startsWith(physId + PhosphorIdentity::VirtualScreenId::Separator))) {
        m_lastNavigationActionKey.clear();
        m_lastNavigationScreenId.clear();
        m_lastNavigationTime.invalidate();
    }
}

void OverlayService::handleScreenRemoved(QScreen* screen)
{
    destroyAllWindowsForPhysicalScreen(screen);
}

OverlayService::LayoutIncludeFlags OverlayService::resolvePerScreenLayoutInclude(const QString& screenId) const
{
    // Both buildLayoutsList (populates the popup) and visibleLayoutCount
    // (used by isNearTriggerEdge to size the keep-visible bar) go through
    // here so the trigger geometry matches the rendered popup row count.
    // If the resolver ever skips setting one of the fields, the struct's
    // in-class defaults (both true) supply a safe "show everything"
    // fallback rather than UB.
    LayoutIncludeFlags flags{m_includeManualLayouts, m_includeAutotileLayouts};
    if (!m_layoutManager) {
        return flags;
    }
    const QString resolvedId = Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)
        ? Phosphor::Screens::ScreenIdentity::idForName(screenId)
        : screenId;
    if (resolvedId.isEmpty()) {
        return flags;
    }
    const QString assignmentId =
        m_layoutManager->assignmentIdForScreen(resolvedId, m_currentVirtualDesktop, m_currentActivity);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
        flags.manual = false;
        flags.autotile = true;
    } else {
        flags.manual = true;
        flags.autotile = false;
    }
    return flags;
}

QVariantList OverlayService::buildLayoutsList(const QString& screenId, QSize autotilePreviewCanvas) const
{
    const auto inc = resolvePerScreenLayoutInclude(screenId);
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, screenId, m_currentVirtualDesktop, m_currentActivity, inc.manual,
        inc.autotile, Utils::screenAspectRatio(m_screenManager, screenId),
        m_settings && m_settings->filterLayoutsByAspectRatio(),
        PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, inc.manual, inc.autotile), m_autotileLayoutSource,
        autotilePreviewCanvas);
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
    // Mirror buildLayoutsList's per-screen include resolution. Pre-fix the
    // raw m_includeManualLayouts/m_includeAutotileLayouts flags were used
    // here — both default true — so on screens where the popup actually
    // showed only manual (or only autotile) layouts, this returned the
    // sum of both, inflating the row count and blowing barHeight up to
    // ~screen height. isNearTriggerEdge then kept the popup visible
    // wherever the cursor was during the drag.
    const auto inc = resolvePerScreenLayoutInclude(screenId);
    // Ordering doesn't affect count — skip custom order for performance.
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, screenId, m_currentVirtualDesktop, m_currentActivity, inc.manual,
        inc.autotile, Utils::screenAspectRatio(m_screenManager, screenId),
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
