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
QQuickItem* slotItemOrNull(const OverlayService::PerScreenOverlayState& state, const QString& key)
{
    if (!state.shell) {
        return nullptr;
    }
    auto it = state.shell->slots.constFind(key);
    return it == state.shell->slots.cend() ? nullptr : it.value().item.data();
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

QQuickItem* OverlayService::PerScreenOverlayState::scrollTabsSlot() const
{
    return slotItemOrNull(*this, PhosphorSlotKeys::ScrollTabs());
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
    externalVulkanInstance = qApp->property(PlasmaZones::PVulkanInstanceProperty).value<QVulkanInstance*>();
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
    // the borrowed pointer is non-null; it only ever receives descriptors when
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

                QObject::connect(&engine, &QObject::destroyed, this, [this]() {
                    m_thumbnailProvider.store(nullptr, std::memory_order_release);
                    m_dmabufTextureProvider.store(nullptr, std::memory_order_release);
                });
            },
        .pipelineCachePath = pipelineCachePath,
        .vulkanInstance = externalVulkanInstance,
        .vulkanApiVersion = PlasmaZones::PVulkanApiVersion,
        // Zero-copy dma-buf thumbnail import (PLASMAZONES_DMABUF_THUMBNAILS)
        // needs these device extensions enabled on the overlay windows' QRhi
        // Vulkan device; without them vkGetMemoryFdPropertiesKHR is unavailable
        // and the import fails. Only requested when the experimental gate is
        // on, so default builds keep Qt's stock device. Qt enables only the
        // physically-supported subset.
        .vulkanDeviceExtensions = qEnvironmentVariableIsSet("PLASMAZONES_DMABUF_THUMBNAILS")
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

    // Connect to screen changes (with safety check for early initialization)
    if (qGuiApp) {
        connect(qGuiApp, &QGuiApplication::screenAdded, this, &OverlayService::handleScreenAdded);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OverlayService::handleScreenRemoved);
    } else {
        qCWarning(lcOverlay) << "Overlay: created before QGuiApplication, screen signals not connected";
    }

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
    if (m_shaderPreviewSurface) {
        m_shaderPreviewSurface->deleteLater();
        m_shaderPreviewSurface = nullptr;
    }

    // Drain deferred-delete events NOW, while all OverlayService members are
    // still alive. Surface destructors may touch m_screenStates, m_shaderRegistry,
    // etc. - if we let ~m_surfaceManager's drain run instead, those members could
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
    QSize initialSize = screenGeom.isValid() ? screenGeom.size() : QSize(240, 70);

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
    // the daemon's lifetime, which exposes composition-pipeline bugs on
    // hybrid-GPU setups. (It no longer masks the compositor's own
    // translucency-while-moving effect: the shell's layer was downgraded from
    // Overlay to Top for exactly that reason, see the PassiveShell role note
    // and issue #516. The rest of the cost stands, which is why the gate
    // stays.) Effects-on path
    // keeps the warm cache; effects-off path lets the next
    // syncSurfaceState !anyVisible transition unmap the wl_surface.
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

    // Same missed-emit case for the tab-strip toggle, which otherwise rides
    // only on scrollingTabIndicatorEnabledChanged: a batch save leaves a live
    // strip painted after the switch was turned off (or an enabled one
    // blank) until the next structural strip change.
    replayScrollTabStrips();

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
    const int virtualDesktop = currentVirtualDesktopForScreen(screenId);
    if (isSnappingContextDisabled(screenId)) {
        return true;
    }
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
        if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
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
    const QStringList screenIds = m_screenStates.keys();
    for (const QString& screenId : screenIds) {
        const bool disabled = isSnappingContextDisabled(screenId);
        if (disabled) {
            destroyZoneSelectorWindow(screenId);
        }
        if (m_visible && isSnappingContextInactive(screenId)) {
            dismissOverlayWindow(screenId);
        }
    }

    // Update remaining zone selector (disabled-gated) and overlay (suppress-gated) windows.
    for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
        const QString& screenId = it.key();
        const bool disabled = isSnappingContextDisabled(screenId);
        if (!disabled && it.value().zoneSelectorSlot()) {
            updateZoneSelectorWindow(screenId);
        }
        if (!isSnappingContextInactive(screenId) && m_visible && it.value().overlayPhysScreen) {
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
        // its flags decided for one id and its rows for another.
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
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
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
    const bool templatesScreen =
        m_layoutSupportResolver && m_layoutSupportResolver(resolvedId) == LayoutSupportTemplates;
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, resolvedId, currentVirtualDesktopForScreen(resolvedId), m_currentActivity,
        inc.manual, inc.autotile, Utils::screenAspectRatio(m_screenManager, resolvedId),
        !templatesScreen && m_settings && m_settings->filterLayoutsByAspectRatio(),
        PhosphorZones::LayoutUtils::buildCustomOrder(m_settings, inc.manual, inc.autotile), m_autotileLayoutSource,
        autotilePreviewCanvas, inc.templates, m_layoutManager ? m_layoutManager->scrollingTemplateStore() : nullptr);
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
    // here - both default true - so on screens where the popup actually
    // showed only manual (or only autotile) layouts, this returned the
    // sum of both, inflating the row count and blowing barHeight up to
    // ~screen height. isNearTriggerEdge then kept the popup visible
    // wherever the cursor was during the drag.
    QString resolvedId;
    const auto inc = resolvePerScreenLayoutInclude(screenId, &resolvedId);
    // Ordering doesn't affect count - skip custom order for performance.
    // Same gate/rows id agreement as buildLayoutsList, including the
    // Templates-screen aspect-filter skip.
    const bool templatesScreen =
        m_layoutSupportResolver && m_layoutSupportResolver(resolvedId) == LayoutSupportTemplates;
    const auto entries = PhosphorZones::LayoutUtils::buildUnifiedLayoutList(
        m_layoutManager, m_algorithmRegistry, resolvedId, currentVirtualDesktopForScreen(resolvedId), m_currentActivity,
        inc.manual, inc.autotile, Utils::screenAspectRatio(m_screenManager, resolvedId),
        !templatesScreen && m_settings && m_settings->filterLayoutsByAspectRatio(),
        /*customOrder=*/{}, m_autotileLayoutSource, /*autotilePreviewCanvas=*/{}, inc.templates,
        m_layoutManager ? m_layoutManager->scrollingTemplateStore() : nullptr);
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
    // Log-only by design: no error latch — shaders retry on the next show
    // (fix bugs, don't mask them).
    qCWarning(lcOverlay) << "Shader error during overlay:" << errorLog;
}

} // namespace PlasmaZones
