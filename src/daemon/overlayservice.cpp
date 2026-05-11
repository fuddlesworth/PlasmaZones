// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overlayservice/internal.h"
#include "overlayservice.h"
#include "snapassistthumbnailprovider.h"

#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorOverlay/ShellHost.h>
#include <PhosphorOverlay/ShellState.h>

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
#include <QDBusConnection>
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
#include "overlayservice/pz_slot_keys.h"
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
    return slotItemOrNull(*this, PzSlotKeys::Osd());
}

QQuickItem* OverlayService::PerScreenOverlayState::snapAssistSlot() const
{
    return slotItemOrNull(*this, PzSlotKeys::SnapAssist());
}

QQuickItem* OverlayService::PerScreenOverlayState::layoutPickerSlot() const
{
    return slotItemOrNull(*this, PzSlotKeys::LayoutPicker());
}

QQuickItem* OverlayService::PerScreenOverlayState::zoneSelectorSlot() const
{
    return slotItemOrNull(*this, PzSlotKeys::ZoneSelector());
}

QQuickItem* OverlayService::PerScreenOverlayState::mainOverlaySlot() const
{
    return slotItemOrNull(*this, PzSlotKeys::MainOverlay());
}

// Per-role SurfaceAnimator config builders + setupSurfaceAnimator +
// applyShaderProfilesToAnimator are extracted to
// overlayservice/animation_config.cpp to keep this translation unit
// under the project's <800-line guideline.

// primeSurfaceRenderPipeline + cancelSurfacePrime are extracted to
// overlayservice/priming.cpp to keep this translation unit under the
// project's <800-line guideline.

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
    // and thread it through every consumer - fail loud if the wiring is
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
    // provider alive - there is no longer any window where a D-Bus
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

    // Phase 2: ShellHost owns the per-screen layer-shell shell-state map
    // and the create / destroy lifecycle. The daemon registers callbacks
    // for the PZ-specific bits - surface factory (PassiveShell role +
    // PassiveOverlayShell.qml + warmed-surface pipeline), post-create
    // slot wiring (5 PZ slot QML object-name lookups + 6 QML signal
    // wires + RHI prime), pre-destroy slot teardown (nulls PZ content
    // sentinels and disconnects per-content connections). The remaining
    // Phase 2 method moves (rekey / sync / validate-invariant) land in
    // follow-on commits.
    m_shellHost = std::make_unique<PhosphorOverlay::ShellHost>(this);
    m_shellHost->setSurfaceAnimator(m_surfaceAnimator.get());

    m_shellHost->setSurfaceFactory([this](const QString& screenId, QScreen* physScreen) -> PhosphorLayer::Surface* {
        const auto role =
            PzRoles::makePerInstanceRole(PzRoles::PassiveShell, screenId, m_surfaceManager->nextScopeGeneration());
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
        auto onVirtualScreensChangedHandler = [this](const QString& physicalScreenId) {
            // Destroy old overlays for this physical screen, recreate with new config
            QScreen* physScreen = Phosphor::Screens::ScreenIdentity::findByIdOrName(physicalScreenId);
            if (!physScreen) {
                // Physical screen removed -- destroy windows and clean up stale virtual screen entries
                // Tear down every shell whose key matches the
                // virtual-screen prefix (`physId/vs:N`) - this catches
                // all sub-region overlays rooted on the just-removed
                // monitor. The bare physId entry is handled separately
                // below; it has no `/vs:N` suffix so it doesn't match
                // the startsWith filter.
                //
                // Two-pass (collect keys, then destroy + erase) because
                // ShellHost::destroyShell's pre-destroy callback
                // (unwirePassiveShellSlots) re-enters m_screenStates by
                // key; a single-pass loop using `it = erase(it)` would
                // invalidate the iterator while the callback is still
                // reading via that same map.
                const QString prefix = physicalScreenId + PhosphorIdentity::VirtualScreenId::Separator;
                QStringList virtualKeysToDestroy;
                for (auto it = m_screenStates.constBegin(); it != m_screenStates.constEnd(); ++it) {
                    if (it.key().startsWith(prefix)) {
                        virtualKeysToDestroy.append(it.key());
                    }
                }
                for (const QString& key : virtualKeysToDestroy) {
                    m_shellHost->destroyShell(key);
                    m_shellHost->removeState(key);
                    m_screenStates.remove(key);
                }
                destroyOverlayWindow(physicalScreenId);
                destroyZoneSelectorWindow(physicalScreenId);
                destroyPassiveShell(physicalScreenId);
                m_shellHost->removeState(physicalScreenId);
                m_screenStates.remove(physicalScreenId);
                // Drop sticky creation-failure flags rooted on the
                // now-removed physical monitor. Without this, a same-
                // name replug would inherit the stale flag and silently
                // refuse to recreate. Mirrors the symmetric clear in
                // destroyAllWindowsForPhysicalScreen (screens.cpp).
                for (const QString& flagged : m_shellHost->failureScreenIds()) {
                    if (flagged == physicalScreenId || flagged.startsWith(prefix)) {
                        m_shellHost->clearFailure(flagged);
                    }
                }
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
                destroyPassiveShell(physicalScreenId);
            }

            // Clear selected zone before destroying windows - the selection references
            // zone geometry from the old virtual screen config and would be stale.
            clearSelectedZone();

            // Track whether zone selectors were visible before destruction so we can
            // recreate them for the new virtual screen configuration.
            const bool hadZoneSelector = m_zoneSelectorVisible;

            // Destroy all window types (overlays, selectors, OSDs, snap assist, layout picker)
            destroyAllWindowsForPhysicalScreen(physScreen);

            // Reset zone selector flag - the windows were destroyed, so the flag
            // must be cleared to allow re-showing. Without this, the guard at the
            // top of showZoneSelector() prevents recreation.
            if (hadZoneSelector) {
                m_zoneSelectorVisible = false;
            }

            // Recreate with new virtual screen config if visible. Reuses
            // mgr2 from above - the Phosphor::Screens::ScreenManager singleton doesn't change
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
                    // back to true - in that case we must NOT call showZoneSelector() again
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
    // Hide overlay + zone-selector slots on screens where the current
    // context is disabled. Post-shell-migration the wl_surface stays
    // mapped (managed by destroyPassiveShell on hot-plug, not per
    // context-toggle); each per-content slot fades out via its
    // configured hide leg. dismissOverlayWindow / hideZoneSelectorSlotOnScreen
    // both clear the per-screen sentinel on completion.
    if (m_settings) {
        const QStringList screenIds = m_screenStates.keys();
        for (const QString& screenId : screenIds) {
            if (isContextDisabled(m_settings, PhosphorZones::AssignmentEntry::Snapping, screenId,
                                  m_currentVirtualDesktop, m_currentActivity)) {
                destroyZoneSelectorWindow(screenId);
                if (m_visible) {
                    dismissOverlayWindow(screenId);
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
        if (it.value().zoneSelectorSlot()) {
            updateZoneSelectorWindow(screenId);
        }
        if (m_visible && it.value().overlayPhysScreen) {
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

// Screen-management methods (setupForScreen / removeScreen /
// assertWindowOnScreen / handleScreenAdded / destroyAllWindowsForPhysicalScreen
// / handleScreenRemoved) are extracted to overlayservice/screens.cpp to keep
// this translation unit under the project's <800-line guideline.

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
    // here - both default true - so on screens where the popup actually
    // showed only manual (or only autotile) layouts, this returned the
    // sum of both, inflating the row count and blowing barHeight up to
    // ~screen height. isNearTriggerEdge then kept the popup visible
    // wherever the cursor was during the drag.
    const auto inc = resolvePerScreenLayoutInclude(screenId);
    // Ordering doesn't affect count - skip custom order for performance.
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
