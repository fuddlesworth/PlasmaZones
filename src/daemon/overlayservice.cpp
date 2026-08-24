// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "rendering/snapassistthumbnailprovider.h"
#include "rendering/dmabuftextureprovider.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorOverlay/ShellState.h>

#include <PhosphorSurfaces/SurfaceManager.h>
#include <PhosphorSurfaces/SurfaceManagerConfig.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include <PhosphorContext/IContextResolver.h>
#include <PhosphorZones/LayoutUtils.h>
#include "common/layoutpreviewserialize.h"
#include "core/utils/unifiedlayoutlist.h"
#include "core/utils/geometryutils.h"
#include <PhosphorScreens/Manager.h>
#include "core/utils/utils.h"
#include "core/types/constants.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDBusConnection>
#include <QGuiApplication>
#include <QScreen>
#include <QByteArrayList>
#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QMutexLocker>

#include "core/platform/logging.h"
#include "phosphor_qml_i18n.h"
#include "rendering/vulkansupport.h"

#include <PhosphorProtocol/ServiceConstants.h>

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
#include "overlayservice/phosphor_roles.h"
#include "overlayservice/phosphor_slot_keys.h"
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {

// Resolve a slot Item from the lib's per-screen slot map. Returns
// nullptr when no shell is wired up, when no slot under @p key was
// populated by the post-create callback, or when the QPointer in the
// map has been cleared because the underlying QQuickItem was destroyed
// (typically: shell torn down out from under us by a deferred signal).
QQuickItem* slotItemOrNull(const PhosphorOverlay::ShellState* shell, const QString& key)
{
    if (!shell) {
        return nullptr;
    }
    auto it = shell->slots.constFind(key);
    return it == shell->slots.cend() ? nullptr : it.value().item.data();
}

QQuickItem* slotItemOrNull(const OverlayService::PerScreenOverlayState& state, const QString& key)
{
    return slotItemOrNull(state.shell, key);
}

} // namespace

QQuickItem* OverlayService::PerScreenOverlayState::osdSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::Osd());
}

QQuickItem* OverlayService::PerScreenOverlayState::snapAssistSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::SnapAssist());
}

QQuickItem* OverlayService::PerScreenOverlayState::layoutPickerSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::LayoutPicker());
}

QQuickItem* OverlayService::PerScreenOverlayState::zoneSelectorSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::ZoneSelector());
}

QQuickItem* OverlayService::PerScreenOverlayState::mainOverlaySlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::MainOverlay());
}

QQuickItem* OverlayService::PerScreenOverlayState::cheatsheetSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::Cheatsheet());
}

QQuickItem* OverlayService::PerScreenOverlayState::scrollDropIndicatorSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::ScrollDropIndicator());
}

// Per-role SurfaceAnimator config builders + setupSurfaceAnimator +
// applyShaderProfilesToAnimator live in overlayservice/animation_config.cpp.

// primeSurfaceRenderPipeline + cancelSurfacePrime live in overlayservice/priming.cpp.

OverlayService::OverlayService(PhosphorScreens::ScreenManager* screenManager, ShaderRegistry* shaderRegistry,
                               PhosphorAnimation::PhosphorProfileRegistry* profileRegistry, QObject* parent)
    : IOverlayService(parent)
    , m_screenProvider(std::make_unique<PhosphorLayer::DefaultScreenProvider>())
    , m_transport(std::make_unique<PhosphorLayer::PhosphorWaylandTransport>())
{
    m_screenManager = screenManager;
    m_shaderRegistry = shaderRegistry;

    // The profile registry is non-optional: SurfaceAnimator binds to it by
    // reference. Composition roots own a single PhosphorProfileRegistry
    // and thread it through every consumer - fail loud if the wiring is
    // wrong rather than silently falling back to library defaults.
    Q_ASSERT_X(profileRegistry, "OverlayService::OverlayService",
               "profileRegistry must not be null: composition root must own and inject the registry");
    if (Q_UNLIKELY(!profileRegistry)) {
        // Release-build twin of the assert above: SurfaceAnimator binds the
        // registry by reference below, so continuing would be an immediate
        // null deref anyway — fail with a diagnosable message instead.
        qFatal("OverlayService: profileRegistry must not be null (composition root wiring error)");
    }

    // Construct ShellHost BEFORE setupSurfaceAnimator: the latter
    // calls applyShaderProfilesToAnimator which routes per-role
    // config writes through m_shellHost->registerConfigForRole.
    // setupSurfaceAnimator wires the animator into the host inline
    // (see animation_config.cpp) so the host has its animator ready
    // by the time applyShaderProfilesToAnimator runs.
    m_shellHost = std::make_unique<PhosphorOverlay::ShellHost>(this);

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
    // Guarded: the screen-signal wiring below explicitly tolerates a null
    // application object, so this read must too — an unguarded qApp deref
    // here contradicted that tolerance three hundred lines apart.
    if (qApp) {
        externalVulkanInstance = qApp->property(PlasmaZones::PVulkanInstanceProperty).value<QVulkanInstance*>();
    }
#endif

    // Construct the thumbnail provider eagerly so the borrowed @c m_thumbnailProvider
    // pointer is non-null from this point onwards. The SurfaceManager (and
    // its engine) is created next; the engineConfigurator releases ownership
    // to the engine once it exists. Until then the unique_ptr keeps the
    // provider alive - there is no longer any window where a D-Bus
    // setSnapAssistThumbnail call would silently drop because the engine
    // hasn't materialised yet.
    m_thumbnailProviderOwned = std::make_unique<SnapAssistThumbnailProvider>();
    m_thumbnailProvider.store(m_thumbnailProviderOwned.get(), std::memory_order_release);

    // Same eager-construct + engine-handover pattern for the zero-copy GPU
    // thumbnail provider (PLASMAZONES_DMABUF_THUMBNAILS). Always constructed so
    // the borrowed pointer is non-null; it only ever receives descriptors while
    // the env gate is on and the kwin-effect takes the dma-buf path.
    m_dmabufTextureProviderOwned = std::make_unique<DmabufTextureProvider>();
    m_dmabufTextureProvider.store(m_dmabufTextureProviderOwned.get(), std::memory_order_release);

    m_surfaceManager = std::make_unique<PhosphorSurfaces::SurfaceManager>(PhosphorSurfaces::SurfaceManagerConfig{
        .surfaceFactory = m_surfaceFactory.get(),
        .engineConfigurator =
            [this](QQmlEngine& engine) {
                auto* localizedContext = new PhosphorLocalizedContext(&engine);
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

                // Mirror for the dma-buf (Texture-type) provider.
                if (!m_dmabufTextureProviderOwned) {
                    m_dmabufTextureProviderOwned = std::make_unique<DmabufTextureProvider>();
                    m_dmabufTextureProvider.store(m_dmabufTextureProviderOwned.get(), std::memory_order_release);
                }
                engine.addImageProvider(QString::fromLatin1(DmabufTextureProvider::ProviderId),
                                        m_dmabufTextureProviderOwned.release());

                // Compare-exchange against the pointers minted for THIS
                // engine: if the configurator ever runs again (re-created
                // engine), the FIRST engine's late destroyed signal must not
                // null out the SECOND engine's freshly-stored providers.
                auto* thumbForThisEngine = m_thumbnailProvider.load(std::memory_order_acquire);
                auto* dmabufForThisEngine = m_dmabufTextureProvider.load(std::memory_order_acquire);
                // Retire the PREVIOUS engine's connection before overwriting
                // the handle: the compare-exchange makes a live fire
                // harmless, but a first-engine connection surviving into
                // ~m_surfaceManager would CAS against already-destructed
                // atomic storage.
                QObject::disconnect(m_engineProviderDestroyConnection);
                m_engineProviderDestroyConnection = QObject::connect(
                    &engine, &QObject::destroyed, this, [this, thumbForThisEngine, dmabufForThisEngine]() {
                        auto* expectedThumb = thumbForThisEngine;
                        m_thumbnailProvider.compare_exchange_strong(expectedThumb, nullptr, std::memory_order_acq_rel);
                        auto* expectedDmabuf = dmabufForThisEngine;
                        m_dmabufTextureProvider.compare_exchange_strong(expectedDmabuf, nullptr,
                                                                        std::memory_order_acq_rel);
                    });
            },
        .pipelineCachePath = pipelineCachePath,
        .vulkanInstance = externalVulkanInstance,
        .vulkanApiVersion = PlasmaZones::PVulkanApiVersion,
        // Zero-copy dma-buf thumbnail import (PLASMAZONES_DMABUF_THUMBNAILS)
        // needs these device extensions enabled on the overlay windows' QRhi
        // Vulkan device; without them vkGetMemoryFdPropertiesKHR is unavailable
        // and the import fails. Skipped when the gate is off, so a session
        // pinned to raw pixels keeps Qt's stock device. Qt enables only the
        // physically-supported subset.
        .vulkanDeviceExtensions = PhosphorProtocol::Service::snapAssistDmabufThumbnailsEnabled()
            ? QByteArrayList{QByteArrayLiteral("VK_KHR_external_memory_fd"),
                             QByteArrayLiteral("VK_EXT_external_memory_dma_buf"),
                             QByteArrayLiteral("VK_EXT_image_drm_format_modifier"),
                             QByteArrayLiteral("VK_KHR_image_format_list")}
            : QByteArrayList{},
    });

    // ShellHost was constructed earlier (before setupSurfaceAnimator)
    // and had its surface animator wired by setupSurfaceAnimator
    // itself. The remaining callbacks (factory + post/pre-create) need
    // m_surfaceManager to exist, so they're registered here.
    m_shellHost->setSurfaceFactory([this](const QString& screenId, QScreen* physScreen) -> PhosphorLayer::Surface* {
        const auto role = PhosphorRoles::makePerInstanceRole(PhosphorRoles::PassiveShell, screenId,
                                                             m_surfaceManager->nextScopeGeneration());
        auto* surface = createWarmedOsdSurface(role, QUrl(QStringLiteral("qrc:/ui/PassiveOverlayShell.qml")),
                                               physScreen, "passive shell", screenId);
        if (!surface) {
            qCWarning(lcOverlay) << "Failed to create passive overlay shell for screen=" << screenId
                                 << ": suppressing further attempts until screen is replugged";
        }
        return surface;
    });

    m_shellHost->setPostCreateCallback([this](const QString& screenId, PhosphorOverlay::ShellState& shellState) {
        wirePassiveShellSlots(screenId, shellState);
    });

    m_shellHost->setPreDestroyCallback([this](const QString& screenId) {
        unwirePassiveShellSlots(screenId);
    });

    // Connect to screen changes. Fail LOUD on a missing application object,
    // matching the profileRegistry pair above: a log-only skip here was a
    // guard that didn't guard — it permanently lost hot-plug tracking for
    // the daemon's whole lifetime with a single warning line as the only
    // trace. Constructing OverlayService before QGuiApplication is a
    // composition-root wiring error, not a runtime condition.
    Q_ASSERT_X(qGuiApp, "OverlayService::OverlayService",
               "must be constructed after QGuiApplication: screen hot-plug tracking cannot be wired late");
    if (Q_UNLIKELY(!qGuiApp)) {
        qFatal(
            "OverlayService: constructed before QGuiApplication (composition-root wiring error) — screen "
            "hot-plug tracking would be permanently lost");
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);

    // Connect to virtual screen configuration changes
    if (auto* mgr = m_screenManager) {
        connect(mgr, &PhosphorScreens::ScreenManager::virtualScreensChanged, this,
                &OverlayService::onVirtualScreensChanged);
        // Regions-only changes (swap/rotate/boundary-resize) also need the
        // overlay windows destroyed and recreated with the new VS geometry.
        // The handler is heavy but only runs when overlays are visible
        // (active drag), so the cost is bounded.
        connect(mgr, &PhosphorScreens::ScreenManager::virtualScreenRegionsChanged, this,
                &OverlayService::onVirtualScreensChanged);
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

    // Disarm the idle quiesce before any teardown below: drainDeferredDeletes
    // runs a nested event loop that dispatches timers, so an armed quiesce
    // whose deadline elapses mid-destruction would fire its lambda re-entrantly
    // against half-destroyed members. disconnect() (not just stop()) so that a
    // future teardown path reaching scheduleIdleQuiesce can restart the timer
    // but never re-establish the timeout connection - it only connects when the
    // pointer is still null.
    if (m_idleQuiesceTimer) {
        m_idleQuiesceTimer->stop();
        m_idleQuiesceTimer->disconnect();
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
    // the window directly - that races against ~Surface and dereferences a
    // deleted pointer in ~Impl.
    //
    // Two-pass over keys-snapshot for the same reason as
    // destroyAllWindowsForPhysicalScreen's virtual-state cleanup: the
    // pre-destroy callback re-reads m_screenStates by key, so we can't
    // mutate during iteration.
    const QStringList screenKeysAtShutdown = m_screenStates.keys();
    for (const QString& screenId : screenKeysAtShutdown) {
        m_shellHost->destroyShell(screenId);
    }

    // Explicit lib teardown BEFORE m_screenStates.clear() so the
    // defense-in-depth PreDestroyCallback re-fire (for any live shell
    // the drain loop above missed) can still touch its parallel
    // per-screen state for cleanup. The lib dtor gates the callback on
    // a live shellSurface (skipping already-drained entries), so the
    // re-fire is a no-op in the steady state - but ordering matters if
    // a future code path leaves a live shell behind.
    m_shellHost.reset();
    m_screenStates.clear();

    // Singleton surfaces (layout picker, shader preview) are QObject
    // children of `this`, so the QObject parent-child system would
    // destroy them AFTER our own destructor body runs - i.e. after
    // the member destructors. Schedule their deletion now so
    // SurfaceManager's drain loop picks them up before the engine is
    // destroyed. Snap-assist post-shell-migration is an Item slot
    // inside the per-screen passive shell - its lifetime is the
    // shell's, no separate cleanup here.
    // Picker post-shell-migration is also a slot in the per-screen
    // passive shell - no separate surface cleanup.
    // COMPLETE hand-rolled subset of destroyShaderPreviewWindow: disconnect
    // the screen-tracking signals and null the raw window pointer BEFORE the
    // drain below destroys the window (the old dtor left
    // m_shaderPreviewWindow dangling across the drain and skipped the
    // disconnect). Deliberately NOT a call to destroyShaderPreviewWindow():
    // its tail schedules the CAVA idle-quiesce, which allocates a QTimer and
    // wires connections inside a destructor — legal but pointless teardown
    // work.
    if (m_shaderPreviewSurface) {
        if (m_shaderPreviewScreen && m_shaderPreviewWindow) {
            disconnect(m_shaderPreviewScreen, nullptr, m_shaderPreviewWindow, nullptr);
        }
        m_shaderPreviewSurface->deleteLater();
        m_shaderPreviewSurface = nullptr;
        m_shaderPreviewWindow = nullptr;
    }

    // Drain deferred-delete events NOW, while all OverlayService members are
    // still alive. Surface destructors may touch m_screenStates, m_shaderRegistry,
    // etc. - if we let ~m_surfaceManager's drain run instead, those members could
    // already be destroyed (C++ member destruction order is reverse declaration).
    m_surfaceManager->drainDeferredDeletes();

    // Retire the engine-destroyed provider null-out lambda and null the
    // atomics here, in the destructor BODY: the engine dies inside
    // ~m_surfaceManager during member destruction, which runs AFTER the two
    // provider atomics' destructors (they are declared later in the class) —
    // letting the lambda fire then would write through destructed storage.
    QObject::disconnect(m_engineProviderDestroyConnection);
    m_thumbnailProvider.store(nullptr, std::memory_order_release);
    m_dmabufTextureProvider.store(nullptr, std::memory_order_release);

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
    // SurfaceConfig::initialSize uses isEmpty() as the "unset" sentinel -
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
    // surface edge - geometry shifted past the surface bounds is dropped
    // by the compositor. A screen-sized OSD surface gives shader effects
    // headroom equal to the screen, and keeps the wiring path identical
    // to popups (which were already screen-sized) so a single
    // `fboExtent` mechanism works uniformly across every overlay role.
    //
    // Cost is real but bearable: a fullscreen swapchain runs ~25 MB at 4K
    // on the NVIDIA proprietary stack, vs ~tens of KB for the content-
    // sized warm-up. With one notification surface per effective screen
    // (~1-6 in typical setups), that's ~25-150 MB. Damage tracking keeps
    // the per-frame cost negligible while idle: a fullscreen surface with
    // a small centred card only repaints the card region.
    QRect screenGeom;
    if (!screenId.isEmpty() && m_screenManager) {
        screenGeom = m_screenManager->screenGeometry(screenId);
    }
    if (!screenGeom.isValid() && physScreen) {
        screenGeom = physScreen->geometry();
    }
    // Content-sized fallback matching the pre-migration OSD toast card (see
    // the swapchain-cost paragraph above for why the surface is normally
    // screen-sized instead).
    static constexpr QSize FallbackOsdToastSize(240, 70);
    QSize initialSize = screenGeom.isValid() ? screenGeom.size() : FallbackOsdToastSize;

    // Virtual-screen-aware anchors / margins, same vocabulary popups use
    // (see the showOnScreen path in selector.cpp). Physical screen →
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

    // keepMappedOnHide is gated on whether any visual effect is enabled.
    // The keep-mapped lifecycle exists so shader / animation transitions
    // do not pay the wl_surface unmap + RHI swapchain teardown cost on
    // every dismiss; with both shaders and animations disabled there is
    // no transition to amortize, and keeping the shell mapped means a
    // fullscreen layer surface is composited above every normal toplevel for
    // the daemon's lifetime, which is a cost an idle daemon should not pay
    // when it is drawing nothing. That is the whole rationale for the gate.
    // It is deliberately NOT a claim about masking the compositor's own
    // translucency-while-moving effect - the PassiveShell role note explains
    // why that mechanism was never real. Effects-on path
    // keeps the warm cache; effects-off path lets the next
    // syncSurfaceState !anyVisible transition unmap the wl_surface.
    //
    // CREATION-TIME ONLY: the gate is evaluated once here and baked into the
    // surface's config for its whole life. Toggling shaders or animations at
    // runtime does not re-configure live shells — a shell created with
    // effects on keeps its mapped fullscreen surface after both are turned
    // off, and one created with effects off pays the unmap/teardown cost on
    // transitions until it is next recreated (hot-plug, VS reconfig). No
    // toggle handler re-creates shells today; a toggle takes effect on the
    // next natural shell creation.
    const bool shadersOn = m_shaderRegistry && m_shaderRegistry->shadersEnabled();
    const bool animationsOn = m_settings && m_settings->animationsEnabled();
    const bool keepMapped = shadersOn || animationsOn;

    auto* surface = createLayerSurface({.qmlUrl = qmlUrl,
                                        .screen = physScreen,
                                        .role = role,
                                        .windowType = windowType,
                                        .anchorsOverride = anchorsOverride,
                                        .marginsOverride = marginsOverride,
                                        .keepMappedOnHide = keepMapped,
                                        .initialSize = initialSize});
    if (!surface) {
        return nullptr;
    }

    // Post-shell-migration: per-content auto-dismiss is wired through
    // the shell window's per-slot signals (`osdDismissRequested`,
    // `snapAssistDismissRequested`, `layoutPickerDismissRequested`),
    // each routed by ensurePassiveShellFor to a slot-specific
    // animator-driven hide rather than a whole-surface hide. There's
    // no generic `dismissRequested` signal on PassiveOverlayShell.qml
    // anymore - wiring one would unmap the shell on any per-slot
    // auto-dismiss timer.
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

    // If the selector was visible but every showing screen is now disabled,
    // hide it immediately. Per-screen and rule-aware: a strip popup is
    // governed by the scrolling toggle, and a SetDragSelectorEnabled rule
    // outranks either toggle, so a live save must not tear down a popup a
    // force-on rule put up (or spare one the scrolling switch turned off).
    if (m_zoneSelectorVisible && m_settings) {
        bool anyEnabled = false;
        for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
            // "Showing" includes logically-visible-but-temporarily-slot-hidden:
            // a modal (snap-assist, the picker) hides the slot WITHOUT touching
            // m_zoneSelectorVisible, and restoreZoneSelectorAfterHide re-shows
            // off the captured (physScreen, geometry) pair — so that pair, not
            // raw slot visibility, is what "still hosting the popup" means.
            // Raw visibility alone let a mid-modal settings save tear down the
            // live selector (every slot hidden -> anyEnabled false). Bare slot
            // EXISTENCE is still not enough ("has a slot" means "has ever
            // hosted the popup"), hence the captured-pair test.
            auto* slot = it.value().zoneSelectorSlot();
            const bool hosting = slot
                && (slot->isVisible()
                    || (it.value().zoneSelectorPhysScreen && it.value().zoneSelectorGeometry.isValid()));
            if (hosting && selectorEnabledForScreen(it.key())) {
                anyEnabled = true;
                break;
            }
        }
        if (!anyEnabled) {
            hideZoneSelector();
        }
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

void OverlayService::setContextResolver(PhosphorContext::IContextResolver* resolver)
{
    m_contextResolver = resolver;
}

bool OverlayService::isSnappingContextDisabled(const QString& screenId) const
{
    // Fail CLOSED on a null resolver, mirroring Daemon::isFocusedContextGated:
    // the resolver is only null before wiring or during teardown, and showing
    // overlay UI in either window is worse than briefly suppressing it.
    if (!m_contextResolver) {
        return true;
    }
    // Deliberately the OPPOSITE default from the null-resolver branch: an
    // empty screenId here is not a teardown window but a caller with no
    // per-screen identity (global paths), and "disabled" for those would
    // suppress every screen's overlay off one anonymous query. Per-screen
    // callers always pass a real id, so the fail-open branch only ever
    // answers the global case.
    if (screenId.isEmpty()) {
        return false;
    }
    PhosphorContext::ContextHandle handle =
        m_contextResolver->handleForPersisted(screenId, currentVirtualDesktopForScreen(screenId), m_currentActivity);
    // Query the SNAPPING axis explicitly, as the name promises. The overlay
    // is snapping-mode UI, and the callers this consolidated previously
    // composed their disable checks with Mode::Snapping; letting the mode
    // provider stamp the screen's CURRENT mode would flip the check to the
    // autotile axis on a tiling screen.
    handle.mode = PhosphorZones::AssignmentEntry::Snapping;
    return m_contextResolver->isDisabled(handle);
}

bool OverlayService::isScrollingContextDisabled(const QString& screenId) const
{
    // The scrolling-axis twin of isSnappingContextDisabled, for the STRIP
    // popup's refresh/destroy gates: a strip screen is exempt from the
    // snapping-axis gate (the popup IS the engine surface that gate
    // protects), but a context the user disabled on the scrolling axis must
    // still tear the popup down. Same fail-closed / fail-open branches as
    // the snapping twin, same mirror-sourced handle.
    if (!m_contextResolver) {
        return true;
    }
    if (screenId.isEmpty()) {
        return false;
    }
    PhosphorContext::ContextHandle handle =
        m_contextResolver->handleForPersisted(screenId, currentVirtualDesktopForScreen(screenId), m_currentActivity);
    handle.mode = PhosphorZones::AssignmentEntry::Scrolling;
    return m_contextResolver->isDisabled(handle);
}

PhosphorZones::Layout* OverlayService::resolveScreenLayout(QScreen* screen) const
{
    // Physical QScreen* overload: derives screenId and delegates.
    // Callers with a known virtual screenId should use the QString overload directly.
    if (!screen) {
        return m_layout;
    }
    return resolveScreenLayout(PhosphorScreens::ScreenIdentity::identifierFor(screen));
}

bool OverlayService::isSnappingContextInactive(const QString& screenId) const
{
    if (isSnappingContextDisabled(screenId)) {
        return true;
    }
    const int virtualDesktop = currentVirtualDesktopForScreen(screenId);
    if (!m_layoutManager) {
        return false;
    }
    if (m_layoutManager->isContextActiveLayoutSuppressed(screenId, virtualDesktop, m_currentActivity)) {
        return true;
    }
    // The context is in an ENGINE mode (autotile or scrolling) — the
    // snapping overlay/selector never applies there. Active engine screens
    // are already kept out via setExcludedScreens; this leg covers the
    // bare/suppressed autotile context. The SCROLLING half additionally
    // consults the LIVE resolver: the router downgrades a scrolling
    // assignment to snapping when the scroll engine does not own the screen
    // (master switch off, Scrolling axis context-disabled), and on such a
    // screen the drag pipeline runs the full snap path and windows really do
    // snap into zones — suppressing the overlay there recreated the
    // drag-vs-overlay disagreement class of #724. With no resolver wired
    // (the shutdown window), the raw read stands, which errs on the
    // suppressed side.
    const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, virtualDesktop, m_currentActivity);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
        // The AUTOTILE half consults its own live resolver for exactly the
        // reason the scrolling half consults the capability one: a downgraded
        // tiling assignment (master switch off, Autotile axis
        // context-disabled) leaves a screen the snap engine really owns, where
        // the drag pipeline runs the full snap path and windows do snap into
        // zones. Suppressing the overlay there is the same drag-vs-overlay
        // disagreement as #724. With no resolver wired (the shutdown window)
        // the raw read stands, erring on the suppressed side.
        return !m_autotileActiveResolver || m_autotileActiveResolver(screenId);
    }
    // The explicit snapping opt-out: no layout is in force, every snap and
    // detection consumer answers null for this screen (layoutForScreen's
    // opt-out arm), so drawing the drag overlay here would render the
    // DEFAULT layout's zones — zones nothing will ever snap into, the
    // drag-vs-overlay disagreement class of #724 in the other direction.
    if (assignmentId == PhosphorZones::NoSnappingLayout) {
        return true;
    }
    if (PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
        return !m_layoutSupportResolver || m_layoutSupportResolver(screenId) == LayoutSupportTemplates;
    }
    return false;
}

PhosphorZones::Layout* OverlayService::resolveScreenLayout(const QString& screenId) const
{
    PhosphorZones::Layout* screenLayout = nullptr;
    if (m_layoutManager && !screenId.isEmpty()) {
        screenLayout =
            m_layoutManager->layoutForScreen(screenId, currentVirtualDesktopForScreen(screenId), m_currentActivity);
        if (!screenLayout) {
            screenLayout = m_layoutManager->defaultLayout();
        }
    }
    if (!screenLayout) {
        screenLayout = m_layout;
    }
    return screenLayout;
}

QString OverlayService::activeLayoutIdForScreen(const QString& screenId) const
{
    // Autotile contexts have no backing Layout object — their active id is the
    // resolved "autotile:<algorithm>" assignment id, which matches the autotile
    // cards in the picker / selector. Live-Templates scrolling contexts answer
    // with the resolved TEMPLATE layout's UUID (the arm below). Manual contexts
    // keep the existing Layout-based resolution (its fallback chain to
    // default/global is what makes snapping highlight correctly).
    if (m_layoutManager && !screenId.isEmpty()) {
        const int virtualDesktop = currentVirtualDesktopForScreen(screenId);
        const QString assignmentId =
            m_layoutManager->assignmentIdForScreen(screenId, virtualDesktop, m_currentActivity);
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)
            && (!m_autotileActiveResolver || m_autotileActiveResolver(screenId))) {
            // Live-gated like the Templates arm below, and for the same
            // reason: on a downgraded tiling assignment the picker offers
            // MANUAL cards, so highlighting the algorithm id would mark a card
            // that is not in the list. Such a screen falls through to the
            // manual resolution instead, which is what its live snapping path
            // uses. An unwired resolver trusts the assignment id.
            //
            // The explicit opt-out stamp: the picker's None card is keyed by
            // the bare reserved word, so "autotile:none" has to translate or
            // the highlight lands on no card. Same string mapping as
            // UnifiedLayoutController::displayIdForAssignment.
            if (PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId) == PhosphorZones::NoTilingAlgorithm) {
                return QString(PhosphorZones::NoSnappingLayout);
            }
            return assignmentId;
        }
        // The snapping opt-out IS the bare reserved word, which already
        // matches the None card — and it must not fall through to the manual
        // resolution below, whose defaultLayout() fallback would highlight
        // the default layout the context just opted out of.
        if (assignmentId == PhosphorZones::NoSnappingLayout) {
            return assignmentId;
        }
        if (PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
            // A scrolling screen's active picker card is its assigned
            // TEMPLATE layout — but only when the scroll engine actually
            // OWNS the screen (live resolver answers Templates). The router
            // downgrades a disabled scrolling assignment to live snapping;
            // highlighting the template there would mark a card the screen
            // is not using while the manual resolution below highlights the
            // snap layout that IS live. With no template (or an unset
            // resolver answering for a live scrolling screen), return the
            // sentinel, which matches nothing.
            const bool liveTemplates =
                !m_layoutSupportResolver || m_layoutSupportResolver(screenId) == LayoutSupportTemplates;
            if (liveTemplates) {
                // Shared authority with UnifiedLayoutController::
                // displayIdForAssignment: template UUID or the sentinel.
                return m_layoutManager->scrollingDisplayIdForContext(screenId, virtualDesktop, m_currentActivity);
            }
            // Downgraded: fall through to the manual resolution below, which
            // highlights the live snapping layout.
        }
    }
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
    return screenLayout ? screenLayout->id().toString() : QString();
}

void OverlayService::hideDisabledAndRefresh()
{
    // Hide overlay + zone-selector slots on screens where the current
    // context is disabled. Post-shell-migration the wl_surface stays
    // mapped (managed by destroyPassiveShell on hot-plug, not per
    // context-toggle); each per-content slot fades out via its
    // configured hide leg. dismissOverlayWindow / hideZoneSelectorSlotOnScreen
    // both clear the per-screen sentinel on completion.
    // The zone selector / layout picker is gated ONLY by the disabled list (it
    // is how a layout gets assigned, so suppress must not hide it); the snap
    // overlay is additionally gated by suppress and by the engine modes
    // (autotile, and live-Templates scrolling) via isSnappingContextInactive.
    // No m_settings gate here: neither context predicate reads settings (both
    // fail closed on their own null members), and skipping the destroy loop on
    // a null settings pointer would leave stale selector/overlay slots up.
    // Evaluate both context predicates ONCE per screen up front: each
    // isSnappingContextInactive call runs isSnappingContextDisabled again
    // internally, and each evaluation does a context resolve plus registry
    // lookups — the previous per-loop re-evaluation paid that up to four
    // times per screen while nothing between the loops changes context.
    struct ContextGates
    {
        bool disabled = false;
        bool inactive = false;
    };
    // The events that route through here (desktop/activity switch, exclusion
    // recompute, settings save) are exactly the ones that can reshape a
    // screen's strip, so drop every memoized card count up front.
    m_stripCardFractionsCache.clear();
    QHash<QString, ContextGates> gates;
    gates.reserve(m_screenStates.size());
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        // Strip-selector screens carry the same exemption showZoneSelector
        // applies: isSnappingContextDisabled answers true for a live Templates
        // scrolling screen, but on a strip screen the popup IS the engine
        // surface that gate protects, so the refresh path must not destroy it
        // on the snapping axis mid-drag. They still gate on their OWN axis:
        // a context disabled for scrolling tears the strip popup down here
        // rather than leaving it to self-heal on the next cursor tick.
        const bool disabled = isStripSelectorScreen(it.key()) ? isScrollingContextDisabled(it.key())
                                                              : isSnappingContextDisabled(it.key());
        gates.insert(it.key(), {disabled, isSnappingContextInactive(it.key())});
    }

    const QStringList screenIds = m_screenStates.keys();
    for (const QString& screenId : screenIds) {
        const ContextGates g = gates.value(screenId);
        if (g.disabled) {
            destroyZoneSelectorWindow(screenId);
        }
        if (m_visible && g.inactive) {
            dismissOverlayWindow(screenId);
        }
    }

    // Update remaining zone selector (disabled-gated) and overlay (suppress-gated) windows.
    // Over a FRESH key snapshot, like the destroy loop above and for the same
    // hazard: every body below reaches back into the service and can create or
    // drop screen states, which would invalidate a live iterator. Fresh rather
    // than the destroy loop's list, because that loop's callbacks can add
    // entries the first snapshot never saw.
    const QStringList refreshScreenIds = m_screenStates.keys();
    for (const QString& screenId : refreshScreenIds) {
        // Fall back to a LIVE evaluation for a screen the snapshot missed
        // (an entry created re-entrantly by the destroy loop's callbacks):
        // gates.value()'s default {false,false} would treat it as enabled
        // AND active, where the pre-cache code evaluated it live.
        const auto gateIt = gates.constFind(screenId);
        const ContextGates g = (gateIt != gates.constEnd())
            ? gateIt.value()
            : ContextGates{isStripSelectorScreen(screenId) ? isScrollingContextDisabled(screenId)
                                                           : isSnappingContextDisabled(screenId),
                           isSnappingContextInactive(screenId)};
        // Visibility-gated like refreshVisibleWindows and the settingsChanged
        // catch-all: showZoneSelector() runs updateZoneSelectorWindow itself
        // before showing, so a hidden selector needs no live re-push — and on
        // a strip screen the re-push now costs a full strip snapshot +
        // serialization, which a desktop switch must not pay for a popup that
        // is not on screen.
        // Each arm looks its state up at the moment it uses it: a screen the
        // destroy loop's callbacks dropped is simply absent, and the selector
        // arm can itself reshape m_screenStates before the overlay arm runs.
        const auto selectorIt = m_screenStates.constFind(screenId);
        if (m_zoneSelectorVisible && !g.disabled && selectorIt != m_screenStates.constEnd()
            && selectorIt->zoneSelectorSlot()) {
            if (isStripSelectorScreen(screenId)) {
                // A desktop/activity switch re-keys the engine state this
                // screen's strip mirrors, so the model AND any stored popup
                // pick both belong to the old context. refreshStripSelector
                // drops the pick and blanks the highlight before re-pushing;
                // a bare updateZoneSelectorWindow would re-push the new list
                // while the old context's indices stay armed for the drop.
                refreshStripSelector(screenId);
            } else {
                updateZoneSelectorWindow(screenId);
            }
        }
        if (!g.inactive && m_visible) {
            const auto overlayIt = m_screenStates.constFind(screenId);
            if (overlayIt != m_screenStates.constEnd() && overlayIt->overlayPhysScreen) {
                updateOverlayWindow(screenId, overlayIt->overlayPhysScreen);
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

int OverlayService::currentVirtualDesktopForScreen(const QString& screenId) const
{
    // Single source of truth: the layout registry owns the per-output desktop
    // map (#648); OverlayService delegates rather than mirroring it, so overlay
    // resolution can never drift from layout resolution. Falls back to the
    // global desktop when no registry is wired.
    return m_layoutManager ? m_layoutManager->currentVirtualDesktopForScreen(screenId) : m_currentVirtualDesktop;
}

void OverlayService::setCurrentActivity(const QString& activityId)
{
    if (m_currentActivity != activityId) {
        m_currentActivity = activityId;
        qCInfo(lcOverlay) << "Activity changed activity=" << activityId;
        hideDisabledAndRefresh();
    }
}

// Screen-management methods (setupForScreen / removeScreen /
// assertWindowOnScreen / handleScreenAdded / destroyAllWindowsForPhysicalScreen
// / handleScreenRemoved) live in overlayservice/screens.cpp.

OverlayService::LayoutIncludeFlags OverlayService::resolvePerScreenLayoutInclude(const QString& screenId,
                                                                                 QString* resolvedIdOut) const
{
    // Both buildLayoutsList (populates the popup) and visibleLayoutCount
    // (used by isNearTriggerEdge to size the keep-visible bar) go through
    // here so the trigger geometry matches the rendered popup row count.
    // The brace-init seeds the manual and autotile fields from the
    // settings-backed member toggles (so the struct's in-class defaults never
    // apply to those two) and leaves templates off, since no member toggle
    // governs it. Every per-screen arm below assigns all three fields
    // outright, so the seed is the answer only for a screen no arm claims.
    LayoutIncludeFlags flags{m_includeManualLayouts, m_includeAutotileLayouts, /*templates=*/false};
    const QString resolvedId = PhosphorScreens::ScreenIdentity::isConnectorName(screenId)
        ? PhosphorScreens::ScreenIdentity::idForName(screenId)
        : screenId;
    if (resolvedIdOut) {
        // The id the include decision was made for — callers must build
        // their layout lists with THIS id, or a connector-name caller gets
        // its flags decided for one id and its rows for another. One
        // exception to the strict agreement: when idForName cannot resolve
        // a connector name, the empty-id bail below returns the SEED flags
        // while this hands back the raw name — both sides then answer from
        // global (non-per-screen) knowledge, which is the closest the two
        // can agree for an unresolvable screen.
        *resolvedIdOut = resolvedId.isEmpty() ? screenId : resolvedId;
    }
    // Engine capability gate FIRST — ahead of the layout-manager guard,
    // which the resolver does not need: only an engine reporting
    // LayoutSupport::None gets no layout list at all (the picker's show
    // bails on the empty list); a Templates screen gets the native template
    // cards via the isScrolling arm below. (The drag-time
    // popup is already suppressed on engine-owned screens by
    // WindowDragAdaptor's dragMoved gate; here that is defence in depth.)
    // The daemon-injected resolver routes through
    // ScreenModeRouter::engineFor, so a disabled/gated scrolling assignment
    // correctly downgrades to snapping and keeps its manual list. The raw
    // assignmentId check below cannot see that downgrade, which is why the
    // scrolling arm consults the resolver too rather than trusting the id.
    if (!resolvedId.isEmpty() && m_layoutSupportResolver && m_layoutSupportResolver(resolvedId) == LayoutSupportNone) {
        flags.manual = false;
        flags.autotile = false;
        flags.templates = false;
        return flags;
    }
    if (!m_layoutManager || resolvedId.isEmpty()) {
        return flags;
    }
    const QString assignmentId = m_layoutManager->assignmentIdForScreen(
        resolvedId, currentVirtualDesktopForScreen(resolvedId), m_currentActivity);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)
        && (!m_autotileActiveResolver || m_autotileActiveResolver(resolvedId))) {
        // Gated on the LIVE resolver for the same reason the Templates arm
        // below is: the router downgrades an autotile assignment to snapping
        // when the tiling engine does not own the screen (master switch off,
        // Autotile axis context-disabled), and such a screen falls through to
        // the manual arm — the list its live snapping path actually uses.
        // Without the gate the picker drew ALGORITHM cards on a screen whose
        // controller list held manual layouts, so every visible card was a
        // dead press. An unwired resolver trusts the assignment id.
        flags.manual = false;
        flags.autotile = true;
        flags.templates = false;
    } else if (PhosphorLayout::LayoutId::isScrolling(assignmentId)
               && (!m_layoutSupportResolver || m_layoutSupportResolver(resolvedId) == LayoutSupportTemplates)) {
        // The live Templates arm: since the native-template pivot a
        // scrolling screen's picker offers TEMPLATE cards only (manual
        // layouts are not templates, algorithms never were). Gated on the
        // LIVE resolver, not the raw assignment id: a downgraded scrolling
        // screen (master switch off, Scrolling axis context-disabled)
        // answers Placement and falls through to the manual arm below,
        // which is the list its live snapping path actually uses. Same
        // shape as isSnappingContextInactive / activeLayoutIdForScreen.
        //
        // An unwired resolver trusts the scrolling assignment id and still
        // yields template cards: the pre-injection default must not blank a
        // scrolling picker. buildLayoutsList and visibleLayoutCount take their
        // aspect-filter skip from this flag rather than re-reading the
        // resolver, so both polarities are decided here, once.
        flags.manual = false;
        flags.autotile = false;
        flags.templates = true;
    } else {
        flags.manual = true;
        flags.autotile = false;
        flags.templates = false;
    }
    return flags;
}

QVariantList OverlayService::buildLayoutsList(const QString& screenId, QSize autotilePreviewCanvas) const
{
    // Gate and rows keyed on the SAME id: the include resolver hands back
    // the id it decided for, so a connector-name caller cannot get flags
    // for the identity id and rows for the raw name.
    QString resolvedId;
    const auto inc = resolvePerScreenLayoutInclude(screenId, &resolvedId);
    // Aspect filtering is a placement heuristic (does this zone layout fit
    // this screen shape); on a Templates screen the same layouts are a
    // SIZING vocabulary, where a portrait-classed column layout is a
    // perfectly good template for an ultrawide, and filtering can empty the
    // candidate list so the picker bails silently. Skip the filter there.
    // Mirrored in visibleLayoutCount below — the two must agree row-for-row.
    //
    // Read off the include flags, not the live resolver: the include arm
    // decides "this screen shows templates" from the assignment id AND the
    // resolver together, and a second, resolver-only read of the same question
    // answers differently wherever the two disagree (an unwired resolver, most
    // of all).
    const bool templatesScreen = inc.templates;
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, resolvedId, currentVirtualDesktopForScreen(resolvedId), m_currentActivity,
        inc.manual, inc.autotile, Utils::screenAspectRatio(m_screenManager, resolvedId),
        !templatesScreen && m_settings && m_settings->filterLayoutsByAspectRatio(),
        PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, inc.manual, inc.autotile, inc.templates),
        m_autotileLayoutSource, autotilePreviewCanvas, inc.templates,
        m_layoutManager ? m_layoutManager->scrollingTemplateStore() : nullptr,
        // The None row: this list is a PICKER of the context's layout (or
        // template), so it carries the opt-out alongside the real choices —
        // template-flavoured on a Templates screen, the generic no-layout row
        // everywhere else. Derived from the include resolution, NOT passed
        // unconditionally: a LayoutSupport::None screen zeroes all three
        // family flags precisely so the picker's show bails on the empty
        // list, and an unconditional row would hand that screen a lone None
        // card whose press writes an opt-out where no layout concept exists.
        // Mirrored in visibleLayoutCount below, which must agree with this
        // row for row.
        inc.manual || inc.autotile || inc.templates,
        // Template cards depict the columns this screen's strip will hold, so
        // they follow the screen's strip axis. Count-neutral, which is why
        // visibleLayoutCount does not pay for the same resolve.
        stripIsVertical(resolvedId));
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
    if (m_excludedScreens == screenIds) {
        return;
    }
    m_excludedScreens = screenIds;
    // A screen entering the excluded set while its overlay is up must drop
    // that overlay now, not on the next incidental refresh — mirror the
    // change-gate + refresh shape of setLayoutFilter. Note the refresh does
    // NOT read m_excludedScreens itself: it works through the
    // isAutotile/isScrolling assignment legs of isSnappingContextInactive,
    // which reflect the same engine-ownership change that drove the
    // exclusion (the caller updates the assignment state before pushing the
    // set here).
    hideDisabledAndRefresh();
}

int OverlayService::selectorCardCount(const QString& screenId) const
{
    // Strip-selector screens: the popup renders strip cards, so the
    // trigger-edge sizing must count THOSE (same row-for-row agreement the
    // layout path keeps with buildLayoutsList below). Floor of 1 matches
    // updateZoneSelectorWindow's empty-strip cell. Kept OUT of
    // visibleLayoutCount so that count stays a pure row count; note the
    // picker/cycle shortcut gates no longer read it as "is the template
    // store empty" at all — the store-independent None row keeps a
    // Templates screen at >= 1 rows, so those gates ask the store's
    // count() directly for their Templates arm.
    if (isStripSelectorScreen(screenId)) {
        return std::max(1, visibleStripCardCount(screenId));
    }
    return visibleLayoutCount(screenId);
}

bool OverlayService::screenResolvesToTemplates(const QString& screenId) const
{
    // Same resolution the row builder and visibleLayoutCount use, so a gate
    // asking "is this list the template vocabulary" can never disagree with
    // the rows the popup would draw.
    return resolvePerScreenLayoutInclude(screenId).templates;
}

int OverlayService::visibleLayoutCount(const QString& screenId) const
{
    // Mirror buildLayoutsList's per-screen include resolution. Pre-fix the
    // raw m_includeManualLayouts/m_includeAutotileLayouts flags were used
    // here - both default true - so on screens where the popup actually
    // showed only manual (or only autotile) layouts, this returned the
    // sum of both, inflating the row count and blowing barHeight up to
    // ~screen height. isNearTriggerEdge then kept the popup visible
    // wherever the cursor was during the drag.
    QString resolvedId;
    const auto inc = resolvePerScreenLayoutInclude(screenId, &resolvedId);
    // Ordering doesn't affect count - skip custom order for performance.
    // Same gate/rows id agreement as buildLayoutsList, including the
    // Templates-screen aspect-filter skip read off the include flags.
    const bool templatesScreen = inc.templates;
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, resolvedId, currentVirtualDesktopForScreen(resolvedId), m_currentActivity,
        inc.manual, inc.autotile, Utils::screenAspectRatio(m_screenManager, resolvedId),
        !templatesScreen && m_settings && m_settings->filterLayoutsByAspectRatio(),
        /*customOrder=*/{}, m_autotileLayoutSource, /*autotilePreviewCanvas=*/{}, inc.templates,
        m_layoutManager ? m_layoutManager->scrollingTemplateStore() : nullptr,
        // Same None row as buildLayoutsList, on the same condition (derived
        // from the include resolution, so a LayoutSupport::None screen counts
        // zero rows and the picker/trigger-edge gates keep refusing). This is
        // the row-for-row agreement the header of that function calls out: a
        // count short by one here would size the popup for fewer cards than
        // it draws.
        inc.manual || inc.autotile || inc.templates
        // No strip axis: this function is asked per cursor tick by the
        // trigger-edge probe, and the axis only transposes each card's bands.
        // It cannot add or drop a row, so taking the default here leaves the
        // row-for-row agreement above intact at no per-tick cost.
    );
    return entries.size();
}

void OverlayService::onPrepareForSleep(bool goingToSleep)
{
    if (goingToSleep) {
        // System going to sleep - nothing to do
        return;
    }

    // System waking up - restart shader timer to avoid large iTimeDelta.
    // Gate on m_visible OR the editor's shader preview — the preview drives
    // the same timer with m_visible == false, and a resume with only the
    // preview on screen used to skip the restart and deliver exactly the
    // giant-delta frame this handler exists to prevent. Deliberately NOT
    // isOverlayDisplaying(): that helper excludes the warm-idled overlay
    // (m_overlayIdled), whose timer stays valid across idle and would
    // deliver the whole suspend duration as the first delta after un-idle —
    // refreshFromIdle's ensureShaderTimerStarted is a no-op on an
    // already-valid timer, so nothing downstream repairs it.
    QMutexLocker locker(&m_shaderTimerMutex);
    const bool previewVisible = m_shaderPreviewWindow && m_shaderPreviewWindow->isVisible();
    if ((m_visible || previewVisible) && m_shaderTimer.isValid()) {
        m_shaderTimer.restart();
        m_lastFrameTime.store(0);
        qCInfo(lcOverlay) << "Shader timer restarted after system resume";
    }
}

void OverlayService::onShaderError(const QString& errorLog)
{
    // Log-only by design: no error latch — shaders retry on the next show
    // (fix bugs, don't mask them).
    qCWarning(lcOverlay) << "Shader error during overlay:" << errorLog;
}

} // namespace PlasmaZones
