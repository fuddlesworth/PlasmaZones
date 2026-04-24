// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusConnection>
#include <QDBusError>
#include <QFile>
#include <QThread>

#include "overlayservice.h"
#include "modetracker.h"
#include "shortcutmanager.h"
#include "rendering/zoneshadernoderhi.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "../config/configbackends.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSource.h>
#include "../core/layoutworker/layoutcomputeservice.h"
#include <PhosphorZones/ZoneDetector.h>
#include "../core/windowregistry.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/constants.h"
#include "../core/geometryutils.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include "../core/logging.h"
#include "../core/screenmoderouter.h"
#include "../core/utils.h"
#include "../config/configdefaults.h"
#include "../config/settingsconfigstore.h"
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include "../core/shaderregistry.h"
#include "../config/settings.h"
#include "../config/configmigration.h"
#include "../config/configbackends.h"
#include "../dbus/layoutadaptor.h"
#include "../dbus/settingsadaptor.h"
#include "../dbus/overlayadaptor.h"
#include "../dbus/zonedetectionadaptor.h"
#include "../dbus/windowtrackingadaptor.h"
#include "../dbus/windowdragadaptor.h"
#include "../dbus/autotileadaptor.h"
#include "../dbus/snapadaptor.h"
#include "../dbus/shaderadaptor.h"
#include "../dbus/compositorbridgeadaptor.h"
#include "../dbus/screenadaptor.h"
#include "../dbus/controladaptor.h"
#include "enginefactory.h"
#include "../autotile/AutotileEngine.h"
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include "../snap/SnapEngine.h"
#include <PhosphorZones/SnapState.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "../common/screenidresolver.h"
#include "../common/layoutbundlebuilder.h"

namespace PlasmaZones {

namespace {
// Debounce interval (ms): coalesce rapid geometry changes (multi-screen, panel editor) into one update.
// Conceptually distinct from DELAYED_PANEL_REQUERY_MS in autotile.cpp (which schedules a
// follow-up panel geometry requery after the debounced update completes).
constexpr int GEOMETRY_UPDATE_DEBOUNCE_MS = 400;
} // anonymous namespace

Daemon::Daemon(QObject* parent)
    : QObject(parent)
    // Don't pass 'this' as parent for unique_ptr-managed objects.
    // unique_ptr owns lifetime; a Qt parent would double-free.
    , m_configBackend(createDefaultConfigBackend())
    , m_layoutManager(std::make_unique<PhosphorZones::LayoutRegistry>(createAssignmentsBackend(),
                                                                      QStringLiteral("plasmazones/layouts")))
    , m_layoutComputeService(std::make_unique<LayoutComputeService>(nullptr))
    , m_settings(std::make_unique<Settings>(m_configBackend.get(), nullptr))
    , m_zoneDetector(std::make_unique<PhosphorZones::ZoneDetector>(nullptr))
    , m_windowRegistry(std::make_unique<WindowRegistry>(nullptr))
    , m_panelSource(std::make_unique<Phosphor::Screens::PlasmaPanelSource>())
    , m_virtualScreenStore(std::make_unique<SettingsConfigStore>(m_settings.get()))
    , m_screenManager(std::make_unique<Phosphor::Screens::ScreenManager>(
          Phosphor::Screens::ScreenManager::Config{
              /*panelSource=*/m_panelSource.get(),
              /*configStore=*/m_virtualScreenStore.get(),
              /*useGeometrySensors=*/true,
              // Align the lib's cap with the daemon's source-of-truth (Settings
              // uses ConfigDefaults::maxVirtualScreensPerPhysical() when
              // validating writes). A lower cap here would silently reject
              // configs Settings accepted, leaving Settings ↔ Phosphor::Screens::ScreenManager
              // divergent.
              /*maxVirtualScreensPerPhysical=*/ConfigDefaults::maxVirtualScreensPerPhysical(),
          },
          nullptr))
    , m_shaderRegistry(std::make_unique<ShaderRegistry>(nullptr))
    , m_overlayService(std::make_unique<OverlayService>(m_screenManager.get(), m_shaderRegistry.get(), nullptr))
    , m_virtualDesktopManager(std::make_unique<VirtualDesktopManager>(m_layoutManager.get(), nullptr))
    , m_activityManager(std::make_unique<ActivityManager>(m_layoutManager.get(), nullptr))
    , m_shortcutManager(std::make_unique<ShortcutManager>(m_settings.get(), m_layoutManager.get(), nullptr))
{
    // Install the layout screen-id resolver before any Daemon-owned machinery
    // starts loading layouts. First-call ensures the once-only install runs
    // exactly once across all Daemon constructions in the process; subsequent
    // Daemons share the already-installed resolver. Moved out of the
    // `QObject((ensureScreenIdResolver(), parent))` comma-operator trick
    // because that idiom reads as an accidental typo.
    ensureScreenIdResolver();

    // Configure geometry update debounce timer
    // This prevents cascading recalculations when multiple geometry changes occur rapidly.
    // Use a longer debounce so KDE panel edit mode exit and other transient
    // changes settle before we recalculate zones and overlay.
    m_geometryUpdateTimer.setSingleShot(true);
    m_geometryUpdateTimer.setInterval(GEOMETRY_UPDATE_DEBOUNCE_MS);
    connect(&m_geometryUpdateTimer, &QTimer::timeout, this, &Daemon::processPendingGeometryUpdates);

    // Wire PhosphorZones::ZoneDetector's adjacency threshold to the settings value. The
    // detector no longer holds an ISettings pointer (it takes just the int)
    // so we mirror the setting here and re-push on change.
    m_zoneDetector->setAdjacentThreshold(m_settings->adjacentThreshold());
    connect(m_settings.get(), &ISettings::adjacentThresholdChanged, this, [this]() {
        m_zoneDetector->setAdjacentThreshold(m_settings->adjacentThreshold());
    });

    // Construct the daemon-owned tile-algorithm registry up front so the
    // layout-source bundle below can bind its autotile source to it. The
    // registry was previously a process-global singleton; per-daemon
    // ownership is the plugin-architecture-friendly shape (see
    // project_plugin_based_compositor.md). Built-in algorithms register
    // automatically in the constructor; scripted algorithms are loaded
    // later by ScriptedAlgorithmLoader during init().
    // Pass nullptr as Qt parent: the unique_ptr owns lifetime and the
    // rest of this ctor follows that convention (see comment above on
    // m_layoutManager et al.).
    m_algorithmRegistry = std::make_unique<PhosphorTiles::AlgorithmRegistry>(nullptr);

    // Build the layout sources here (rather than later in init()) because they
    // are thin wrappers — no I/O, no signal hookup — and consumers can ask for
    // layoutSource() any time after Daemon is constructed.  Population happens
    // lazily on first availableLayouts() call: the layout manager has loaded
    // from disk by then, and the algorithm registry is populated by
    // ScriptedAlgorithmLoader during init().
    //
    // Auto-discovery pattern: every provider library that links into
    // this process registers a builder in its static-init block. The
    // daemon just publishes the registries it owns into the
    // FactoryContext and calls buildFromRegistered (both steps are
    // wrapped in buildStandardLayoutSourceBundle — shared with editor
    // + settings so service additions touch one helper rather than
    // three near-identical blocks). Adding a new engine library
    // (the planned scrolling engine) is purely a library-side change
    // — daemon source only edits if the new engine demands a service
    // the daemon doesn't already publish here. ZonesLayoutSource and
    // AutotileLayoutSource both self-wire to their registry's
    // ILayoutSourceRegistry::contentsChanged signal, so no manual
    // bridging is required after build.
    buildStandardLayoutSourceBundle(m_layoutSources, m_layoutManager.get(), m_algorithmRegistry.get());
    // Cache the bundle's autotile source once so the four init() wiring
    // sites that need it don't each re-call source(QStringLiteral("autotile"))
    // (one literal typo away from silently breaking preview-cache reuse).
    m_autotileLayoutSource = m_layoutSources.source(PhosphorTiles::autotileLayoutSourceName());
}

Daemon::~Daemon()
{
    stop();
}

bool Daemon::init()
{
    // Settings constructor already calls load(); avoid duplicate load

    // QShaderBaker/glslang is not thread-safe — concurrent bake() calls crash
    // in QSpirvCompiler::compileToSpirv(). Limit to 1 thread so bakes are
    // sequential but still off the main thread.
    m_shaderBakePool.setMaxThreadCount(1);

    // Warm cached shader bakes on every registry refresh so overlay paints
    // never block the GUI thread waiting for qsb. m_shaderRegistry itself
    // is constructed in the ctor init list (before m_overlayService, which
    // borrows it).
    auto scheduleWarmForShader =
        [this, registryPtr = QPointer<ShaderRegistry>(m_shaderRegistry.get())](const ShaderRegistry::ShaderInfo& info) {
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
                    qCWarning(lcDaemon) << "Shader bake: failed for" << shaderId << r.errorMessage;
                }
                registryPtr->reportShaderBakeFinished(shaderId, r.success, r.errorMessage);
                watcher->deleteLater();
            });
            reg->reportShaderBakeStarted(shaderId);
            // Pass the registry's authoritative search paths to the bake worker
            // so include resolution matches the on-screen render path exactly.
            // Snapshot now (registry can be mutated on the GUI thread; we're about
            // to hop onto the bake thread).
            const QStringList includePaths = reg->searchPaths();
            watcher->setFuture(QtConcurrent::run(
                &m_shaderBakePool, [vertPath = info.vertexShaderPath, fragPath = info.sourcePath, includePaths]() {
                    return warmShaderBakeCacheForPaths(vertPath, fragPath, includePaths);
                }));
        };
    connect(m_shaderRegistry.get(), &ShaderRegistry::shadersChanged, this, [this, scheduleWarmForShader]() {
        const QList<ShaderRegistry::ShaderInfo> shaders = m_shaderRegistry->availableShaders();
        for (const ShaderRegistry::ShaderInfo& info : shaders) {
            scheduleWarmForShader(info);
        }
    });
    // Warm cache once for shaders already loaded by ShaderRegistry ctor
    for (const ShaderRegistry::ShaderInfo& info : m_shaderRegistry->availableShaders()) {
        scheduleWarmForShader(info);
    }

    // PhosphorZones::LayoutRegistry now takes a free-function provider rather than an
    // ISettings pointer — keeps the lib-side class out of project-side
    // interface knowledge. Settings is owned by `this` (daemon) and
    // outlives the layout manager (declared earlier in daemon.h), so
    // the captured pointer is safe.
    m_layoutManager->setDefaultLayoutIdProvider([this]() {
        return m_settings->defaultLayoutId();
    });
    // Wire the compute service to the layout manager so tracked layouts
    // are evicted on removal (bounds m_trackedLayouts over time).
    m_layoutComputeService->setLayoutManager(m_layoutManager.get());

    // Load layouts (defaultLayout() reads settings internally)
    m_layoutManager->loadLayouts();
    m_layoutManager->loadAssignments();

    // Recalculate zone geometries for ALL layouts so that fixed-mode zones
    // have correct normalized coordinates for preview rendering (KCM, OSD, selector).
    if (QScreen* primary = Utils::primaryScreen()) {
        for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
            LayoutComputeService::recalculateSync(
                layout, GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, primary));
        }
    }

    // Configure overlay service with settings, layout manager, and default
    // layout. ShaderRegistry is wired via the ctor, so every overlay path
    // that needs it sees a non-null registry from the first call onward.
    m_overlayService->setSettings(m_settings.get());
    m_overlayService->setLayoutManager(m_layoutManager.get());
    m_overlayService->setAlgorithmRegistry(m_algorithmRegistry.get());
    m_overlayService->setAutotileLayoutSource(m_autotileLayoutSource);
    if (auto* defLayout = m_layoutManager->defaultLayout()) {
        m_overlayService->setLayout(defLayout);
        m_zoneDetector->setLayout(defLayout);
        qCInfo(lcDaemon) << "Overlay configured layout=" << defLayout->name() << "zones=" << defLayout->zoneCount();
    } else {
        qCWarning(lcDaemon) << "No default layout available for overlay";
    }

    // Connect layout changes to zone detector and overlay service
    // activeLayoutChanged fires when the global active layout changes; layoutAssigned
    // fires for per-screen assignments. We handle both but avoid redundant recalculations.
    connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::activeLayoutChanged, this,
            [this](PhosphorZones::Layout* layout) {
                if (layout) {
                    // Recalculate zone geometries asynchronously using primary screen geometry.
                    // Active layout is global; recalculating per-screen overwrites each
                    // iteration (last-wins bug). The overlay computes per-screen geometry
                    // on the fly via GeometryUtils::getZoneGeometryWithGaps(m_screenManager.get(), ).
                    QScreen* primary = Utils::primaryScreen();
                    if (primary) {
                        QString screenId = Phosphor::Screens::ScreenIdentity::identifierFor(primary);
                        m_layoutComputeService->requestRecalculate(
                            layout, screenId,
                            GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, primary));
                    }
                }
                m_zoneDetector->setLayout(layout);
                m_overlayService->updateLayout(layout);
            });

    // Connect per-screen layout assignments
    // Only update if this is a DIFFERENT layout than the active one
    // (to avoid double-processing when both signals fire for the same layout)
    connect(m_layoutManager.get(), &PhosphorZones::LayoutRegistry::layoutAssigned, this,
            [this](const QString& screenId, int /*virtualDesktop*/, PhosphorZones::Layout* layout) {
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
                QScreen* screen = m_screenManager->screenByName(screenId);
                if (screen) {
                    m_layoutComputeService->requestRecalculate(
                        layout, screenId,
                        GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, screen));
                }
                // Note: We don't change zone detector or overlay here since
                // they work with the active layout, not per-screen layouts
            });

    // Connect settings changes to overlay service and autotile engine.
    // This is the SINGLE comprehensive handler for batch config reloads (Settings::load()).
    // Individual autotile signals are NOT emitted from load() — all autotile state
    // transitions are handled here to avoid redundant retile passes.
    m_prevSnappingEnabled = m_settings->snappingEnabled();
    m_prevAutotileEnabled = m_settings->autotileEnabled();
    connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());

        // Detect state transitions before syncing
        const bool snappingNow = m_settings->snappingEnabled();
        const bool autotileNow = m_settings->autotileEnabled();
        const bool snappingToggled = snappingNow != m_prevSnappingEnabled;
        const bool autotileToggled = autotileNow != m_prevAutotileEnabled;
        m_prevSnappingEnabled = snappingNow;
        m_prevAutotileEnabled = autotileNow;

        // Capture old preview params before sync to detect tiling parameter changes
        const auto prevPreviewParams =
            m_algorithmRegistry ? m_algorithmRegistry->previewParams() : PhosphorTiles::AlgorithmPreviewParams{};

        // Sync engine config (idempotent — skips retile if nothing changed)
        if (m_autotileEngine) {
            m_autotileEngine->syncFromSettings(m_settings.get());
        }

        // If tiling preview parameters changed (maxWindows, masterCount, splitRatio),
        // notify layout list consumers to refetch with updated previews
        if (m_algorithmRegistry && m_algorithmRegistry->previewParams() != prevPreviewParams && m_layoutAdaptor) {
            m_layoutAdaptor->notifyLayoutListChanged();
        }

        // Capture autotile window order BEFORE any mode switch destroys PhosphorTiles::TilingState.
        // Saved for deterministic re-seeding when autotile is re-enabled.
        if (autotileToggled && !autotileNow) {
            m_lastAutotileOrders = captureAutotileOrders();
        }

        // Handle autotile feature gate toggle
        if (autotileToggled && !autotileNow) {
            handleAutotileDisabled();
        }

        // Handle activation of autotile mode.
        // Fires when either:
        //   (a) Snapping toggled OFF while autotile is already enabled, OR
        //   (b) Autotile toggled ON (regardless of snapping state)
        // Both paths need per-screen autotile assignments created.
        // handleSnappingToAutotile() skips screens already on an autotile
        // assignment, so mixed-mode setups (screen A snapping, screen B
        // autotile) correctly flip screen A without clobbering screen B's
        // per-screen algorithm customization.
        const bool enteringAutotile =
            (snappingToggled && !snappingNow && autotileNow) || (autotileToggled && autotileNow && !snappingNow);
        if (enteringAutotile) {
            handleSnappingToAutotile();
        }

        // Re-derive autotile screens and apply per-screen overrides.
        // windowsReleased clears floating state for released windows.
        updateAutotileScreens();
        updateLayoutFilter();

        // Resnap after autotile disabled: restore windows to their pre-autotile
        // zone positions. PhosphorZones::Zone assignments are preserved during autotile (onLayoutChanged
        // skips autotile screens) so resnap uses original snap assignments.
        if (autotileToggled && !autotileNow && m_windowTrackingAdaptor) {
            m_suppressResnapOsd = 1;
            m_snapAdaptor->resnapCurrentAssignments();
            restoreAutotileOnlyGeometries();
        }

        // Re-resolve the active layout from assignments for the current context.
        // Resnap/retile/OSD is triggered separately by applyAssignmentChanges()
        // after the KCM's batch save completes — NOT here in the settings handler.
        syncModeFromAssignments();
    });

    // Initialize domain-specific D-Bus adaptors
    // Each adaptor has its own D-Bus interface
    // D-Bus adaptors use raw new; Qt parent-child manages their lifetime.
    m_layoutAdaptor =
        new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), m_screenManager.get(), this);
    m_layoutAdaptor->setActivityManager(m_activityManager.get());
    m_layoutAdaptor->setSettings(m_settings.get());
    m_layoutAdaptor->setLayoutSource(m_layoutSources.composite());
    // Thread the bundle-owned autotile source through the adaptor's
    // buildUnifiedLayoutList path so its preview cache survives across
    // D-Bus calls. The full composite above drives the
    // getLayoutPreview* methods; this separate pointer targets only the
    // autotile enumeration slot — see LayoutAdaptor::setAutotileLayoutSource.
    m_layoutAdaptor->setAutotileLayoutSource(m_autotileLayoutSource);
    // Invalidate D-Bus getActiveLayout() cache when the default layout changes in settings
    connect(m_settings.get(), &Settings::defaultLayoutIdChanged, m_layoutAdaptor, &LayoutAdaptor::invalidateCache);
    m_settingsAdaptor = new SettingsAdaptor(m_settings.get(), m_shaderRegistry.get(), this);

    // Shader adaptor - shader discovery, compilation lifecycle, file monitoring.
    // Held as a member so stop() can detach() it before the unique_ptr member
    // that owns m_shaderRegistry runs its destructor.
    m_shaderAdaptor = new ShaderAdaptor(m_shaderRegistry.get(), this);

    // Compositor bridge adaptor - compositor-agnostic window control protocol.
    // Captured as a local so we can wire its bridgeRegistered signal to
    // WindowDragAdaptor::resetDragState below. Ownership stays with `this`
    // via QObject parent (passed to constructor).
    auto* compositorBridge = new CompositorBridgeAdaptor(this);

    // Overlay adaptor - overlay visibility and highlighting
    m_overlayAdaptor = new OverlayAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                          m_screenManager.get(), m_settings.get(), this);

    // PhosphorZones::Zone detection adaptor - zone detection queries
    m_zoneDetectionAdaptor = new ZoneDetectionAdaptor(m_zoneDetector.get(), m_layoutManager.get(),
                                                      m_screenManager.get(), m_settings.get(), this);

    // Window tracking adaptor - window-zone assignments
    m_windowTrackingAdaptor =
        new WindowTrackingAdaptor(m_layoutManager.get(), m_zoneDetector.get(), m_screenManager.get(), m_settings.get(),
                                  m_virtualDesktopManager.get(), this);
    m_windowTrackingAdaptor->setZoneDetectionAdaptor(m_zoneDetectionAdaptor);
    m_windowTrackingAdaptor->setWindowRegistry(m_windowRegistry.get());

    // Reapply window geometries after each geometry batch (processPendingGeometryUpdates).
    // When the delayed panel requery completes it emits availableGeometryChanged, which triggers
    // the same debounce → processPendingGeometryUpdates → reapply path; no separate delay needed.
    m_reapplyGeometriesTimer.setSingleShot(true);
    connect(&m_reapplyGeometriesTimer, &QTimer::timeout, m_windowTrackingAdaptor,
            &WindowTrackingAdaptor::requestReapplyWindowGeometries);

    // ScreenAdaptor::setVirtualScreenConfig writes to Settings (the source of
    // truth) via the IConfigStore — the daemon's single SettingsConfigStore
    // instance, shared with m_screenManager (as its Config::configStore) and
    // m_virtualScreenSwapper. One store per process, one change-signal
    // channel, no parallel Settings observer.
    m_screenAdaptor = new ScreenAdaptor(m_screenManager.get(), m_virtualScreenStore.get(), this);

    // Window drag adaptor - handles drag events from KWin script
    // All drag logic (modifiers, zones, snapping) handled here
    m_windowDragAdaptor = new WindowDragAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                                m_screenManager.get(), m_settings.get(), m_windowTrackingAdaptor, this);

    // PhosphorZones::Zone selector methods are called directly from WindowDragAdaptor; QDBusAbstractAdaptor
    // signals are for D-Bus, not Qt connections.

    // Give the window drag adaptor access to the shortcut manager for
    // registering/unregistering the Escape cancel shortcut during drags.
    // Routed through the Phosphor::Shortcuts::Integration::IAdhocRegistrar interface so the underlying
    // Registry stays private to ShortcutManager.
    m_windowDragAdaptor->setShortcutRegistrar(m_shortcutManager.get());

    // When the compositor bridge re-registers (e.g. KWin reloaded the effect,
    // effect process restarted, or daemon itself restarted mid-drag), any drag
    // state the daemon is still holding is stale — the new effect instance has
    // no knowledge of the prior drag. Clear it eagerly so the next dragStarted
    // from the fresh effect lands on a clean slate instead of silently
    // colliding with a mismatched windowId in the next handler.
    connect(compositorBridge, &CompositorBridgeAdaptor::bridgeRegistered, m_windowDragAdaptor,
            [this](const QString& compositorName, const QString&, const QStringList&) {
                qCInfo(lcDaemon) << "Compositor bridge registered (" << compositorName
                                 << ") — clearing any stale drag state held by daemon";
                m_windowDragAdaptor->clearForCompositorReconnect();
            });

    // Initialize scripted algorithm loader BEFORE engine construction so that
    // user-defined algorithms are registered in the daemon registry before
    // the engine resolves the configured algorithm ID.
    m_scriptedAlgorithmLoader = std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(
        QString(ScriptedAlgorithmSubdir), m_algorithmRegistry.get());
    // When scripted algorithms change (hot-reload), notify layout list consumers
    connect(m_scriptedAlgorithmLoader.get(), &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged, this,
            [this]() {
                if (m_layoutAdaptor)
                    m_layoutAdaptor->notifyLayoutListChanged();
            });
    m_scriptedAlgorithmLoader->scanAndRegister();

    // Create both placement engines and the mode router via factory.
    // The factory returns concrete types; we grab raw pointers for adaptor
    // wiring before moving into the base-class unique_ptr members.
    auto engines = createEngines(m_layoutManager.get(), m_windowTrackingAdaptor->service(), m_screenManager.get(),
                                 m_algorithmRegistry.get(), m_zoneDetector.get(), m_settings.get(),
                                 m_virtualDesktopManager.get(), m_windowRegistry.get(), this);
    auto* autotileEngine = engines.autotile.get();
    auto* snapEngine = engines.snap.get();
    m_autotileEngine = std::move(engines.autotile);
    m_snapEngine = std::move(engines.snap);
    m_screenModeRouter = std::move(engines.router);

    autotileEngine->syncFromSettings(m_settings.get());
    autotileEngine->connectToSettings(m_settings.get());

    // Give the window drag adaptor access to the autotile engine for per-screen
    // autotile checks (overlay suppression and snap rejection on autotile screens).
    // Uses the base-class pointer — WDA only needs isActiveOnScreen().
    m_windowDragAdaptor->setAutotileEngine(m_autotileEngine.get());

    // SnapEngine creates its own SnapState internally (symmetric with
    // AutotileEngine/TilingState). WTS references it for zone queries.
    m_windowTrackingAdaptor->service()->setSnapState(snapEngine->snapState());
    m_windowTrackingAdaptor->service()->setSnapEngine(snapEngine);

    // Wire persistence delegate — SnapEngine delegates save/load to WTA's KConfig layer.
    // QPointer guards against late calls during shutdown if WTA is destroyed first.
    snapEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
        });

    // Wire engine cross-references (SnapEngine ↔ AutotileEngine, zone detection).
    m_windowTrackingAdaptor->setEngines(snapEngine, autotileEngine);

    // Wire SnapEngine's back-reference to the window tracking adaptor.
    // SnapEngine's navigation methods (focusInDirection, moveFocusedInDirection, …)
    // were moved out of WindowTrackingAdaptor and need to reach back into the
    // adaptor for shared state that hasn't been migrated yet: the target
    // resolver, the last-active window/screen shadow, and the snap-
    // bookkeeping helpers (windowSnapped, windowUnsnapped, recordSnapIntent,
    // clearPreTileGeometry). A future refactor should move that state onto
    // SnapEngine or WindowTrackingService and retire the back-reference.
    snapEngine->setWindowTrackingAdaptor(m_windowTrackingAdaptor);

    // Clear stale autotile-floated flag when a window is snapped. A window
    // dragged from an autotile VS to a snap VS retains its autotileFloated
    // marker; without this, a subsequent mode change on the autotile VS
    // incorrectly processes the already-snapped window as autotile-managed.
    // Wired here (daemon) because engines must not know about each other.
    connect(snapEngine, &SnapEngine::windowSnapStateChanged, this,
            [this](const QString& windowId, const WindowStateEntry&) {
                if (m_autotileEngine) {
                    m_autotileEngine->clearModeSpecificFloatMarker(windowId);
                }
            });

    // ScreenModeRouter was created by createEngines() above; wire it to WTA.
    m_windowTrackingAdaptor->setScreenModeRouter(m_screenModeRouter.get());

    // m_virtualScreenStore is constructed in the initializer list (it's a
    // Config arg for m_screenManager). The swapper is constructed here
    // because navigation handlers don't run before init() returns anyway.
    m_virtualScreenSwapper = std::make_unique<Phosphor::Screens::VirtualScreenSwapper>(m_virtualScreenStore.get());
    Q_ASSERT(m_virtualScreenSwapper);

    // Wire autotile persistence through WTA's KConfig layer (same delegate pattern as SnapEngine).
    // Note: engine->saveState() intentionally triggers a full WTA save (all window tracking
    // state, not just autotile). This is heavier than a targeted save but ensures consistency
    // — the autotile window orders are embedded in WTA's save cycle via the serialization
    // delegates below. The engine-level delegates exist to satisfy the IPlacementEngine interface.
    // QPointer guards against late calls during shutdown if WTA is destroyed first.
    autotileEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
        });
    autotileEngine->setIsWindowFloatingFn([wta = QPointer(m_windowTrackingAdaptor)](const QString& windowId) -> bool {
        return wta && wta->service() && wta->service()->isWindowFloating(windowId);
    });

    // Wire window order serialization delegates so WTA includes autotile window
    // orders in its save/load cycle (analogous to WindowZoneAssignmentsFull for snap mode)
    m_windowTrackingAdaptor->setTilingStateDelegates(
        [engine = QPointer(autotileEngine)]() -> QJsonArray {
            return engine ? engine->serializeWindowOrders() : QJsonArray{};
        },
        [engine = QPointer(autotileEngine)](const QJsonArray& orders) {
            if (engine)
                engine->deserializeWindowOrders(orders);
        });

    m_windowTrackingAdaptor->setTilingPendingRestoreDelegates(
        [engine = QPointer(autotileEngine)]() -> QJsonObject {
            return engine ? engine->serializePendingRestores() : QJsonObject{};
        },
        [engine = QPointer(autotileEngine)](const QJsonObject& obj) {
            if (engine)
                engine->deserializePendingRestores(obj);
        });

    // Trigger WTA save on autotile state changes (window order, split ratio, master count).
    // Narrower dirty mask than the default DirtyAll — only the two autotile-owned
    // fields can change as a result of a placementChanged signal, so the next save
    // rewrites just those keys rather than the whole window-tracking blob.
    //
    // markDirty() emits WindowTrackingService::stateChanged, which is wired to
    // WindowTrackingAdaptor::scheduleSaveState in the adaptor's constructor —
    // that connection is what actually kicks the debounced save timer. If the
    // stateChanged hookup ever gets severed, autotile state will silently
    // stop persisting; add an explicit scheduleSaveState() call here if so.
    connect(autotileEngine, &PhosphorEngineApi::PlacementEngineBase::placementChanged, m_windowTrackingAdaptor,
            [this]() {
                if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
                    m_windowTrackingAdaptor->service()->markDirty(WindowTrackingService::DirtyAutotileOrders
                                                                  | WindowTrackingService::DirtyAutotilePending);
                }
            });

    // Create engine D-Bus adaptors — each engine has a dedicated adaptor that
    // connects signals in its constructor (unified pattern for both engines)
    m_snapAdaptor = new SnapAdaptor(snapEngine, m_windowTrackingAdaptor, m_settings.get(), this);
    m_snapAdaptor->setScreenModeRouter(m_screenModeRouter.get());
    m_autotileAdaptor = new AutotileAdaptor(autotileEngine, m_screenManager.get(), m_algorithmRegistry.get(), this);

    // Control adaptor - high-level convenience API for third-party integrations.
    // Held as a member so stop() can detach() it before the unique_ptr members
    // it borrows are destroyed.
    m_controlAdaptor = new ControlAdaptor(m_windowTrackingAdaptor, m_snapAdaptor, m_layoutAdaptor,
                                          m_layoutManager.get(), autotileEngine, m_screenManager.get(), this);

    // Handle KCM assignment change resnap/OSD. This runs AFTER the KCM's batch
    // save completes (all setAssignmentEntry + notifyReload finished), so all
    // assignments and settings are fully committed. Separated from settingsChanged
    // handler to avoid feedback loops with autotile/snapping transitions.
    connect(
        m_layoutAdaptor, &LayoutAdaptor::assignmentChangesApplied, this,
        [this](const QStringList& changedScreenIdsList) {
            const QSet<QString> changedScreenIds(changedScreenIdsList.begin(), changedScreenIdsList.end());
            if (!m_snapEngine || !m_windowTrackingAdaptor || !m_screenManager || !m_layoutManager)
                return;

            const int desktop = currentDesktop();
            const QString activity = currentActivity();

            // Collect autotile screens and per-screen OSD data in one pass
            QSet<QString> autotileScreens;
            struct ScreenOsd
            {
                QString screenId;
                bool isAutotile;
                QString algoId;
            };
            QVector<ScreenOsd> osdEntries;
            const QStringList effectiveIds = m_screenManager->effectiveScreenIds();
            for (const QString& screenId : effectiveIds) {
                const QString assignmentId = m_layoutManager->assignmentIdForScreen(screenId, desktop, activity);
                if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
                    autotileScreens.insert(screenId);
                }
                // Only show OSD for screens that actually changed
                if (changedScreenIds.isEmpty() || changedScreenIds.contains(screenId)) {
                    if (autotileScreens.contains(screenId)) {
                        osdEntries.append({screenId, true, PhosphorLayout::LayoutId::extractAlgorithmId(assignmentId)});
                    } else {
                        osdEntries.append({screenId, false, {}});
                    }
                }
            }

            // Resnap only the snapping-mode screens whose assignments actually changed.
            // changedScreenIds scopes the resnap to avoid spurious geometry-set on
            // screens whose layout didn't change (prevents flicker on unrelated VS).
            m_suppressResnapOsd = osdEntries.size();
            m_windowTrackingAdaptor->service()->populateResnapBufferForAllScreens(autotileScreens, changedScreenIds);
            m_snapAdaptor->resnapToNewLayout();

            // Show OSD for changed screens — use locked OSD variant when context is locked
            for (const auto& osd : std::as_const(osdEntries)) {
                int mode = osd.isAutotile ? 1 : 0;
                if (isCurrentContextLockedForMode(osd.screenId, mode)) {
                    showLockedPreviewOsd(osd.screenId);
                } else if (osd.isAutotile) {
                    if (!osd.algoId.isEmpty())
                        showLayoutOsdForAlgorithm(osd.algoId, osd.algoId, osd.screenId);
                } else {
                    PhosphorZones::Layout* layout = m_layoutManager->layoutForScreen(osd.screenId, desktop, activity);
                    if (layout)
                        showLayoutOsd(layout, osd.screenId);
                }
            }
        });

    // Register D-Bus service and object with error handling and retry logic
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCCritical(lcDaemon) << "Session D-Bus: cannot connect, daemon cannot function";
        return false;
    }

    // Retry D-Bus service registration with exponential backoff.
    // Synchronous retry is required here because init() runs before QGuiApplication::exec(),
    // so QTimer-based async approaches won't fire. Delays are kept short (700ms total max).
    constexpr int maxRetries = 3;
    constexpr int baseDelayMs = 100; // 100ms, 200ms, 400ms exponential backoff
    bool serviceRegistered = false;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (bus.registerService(QString(PhosphorProtocol::Service::Name))) {
            serviceRegistered = true;
            break;
        }

        QDBusError error = bus.lastError();
        if (error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::NoReply) {
            // Transient error - retry with exponential backoff
            if (attempt < maxRetries - 1) {
                const int delayMs = baseDelayMs * (1 << attempt);
                qCWarning(lcDaemon) << "D-Bus service registration: failed (attempt" << (attempt + 1) << "/"
                                    << maxRetries << ")," << error.message() << "retrying in" << delayMs << "ms";
                QThread::msleep(delayMs);
                continue;
            }
        }

        // Non-retryable error or max retries reached
        qCCritical(lcDaemon) << "Failed to register D-Bus service=" << PhosphorProtocol::Service::Name
                             << "error=" << error.message() << "type=" << error.type();
        return false;
    }

    if (!serviceRegistered) {
        qCCritical(lcDaemon) << "Failed to register D-Bus service after" << maxRetries << "attempts";
        return false;
    }

    // Register D-Bus object (no retry needed - service is already registered)
    if (!bus.registerObject(QString(PhosphorProtocol::Service::ObjectPath), this)) {
        QDBusError error = bus.lastError();
        qCCritical(lcDaemon) << "Failed to register D-Bus object=" << PhosphorProtocol::Service::ObjectPath
                             << "error=" << error.message();
        // Cleanup: unregister service if object registration fails
        bus.unregisterService(QString(PhosphorProtocol::Service::Name));
        return false;
    }

    qCInfo(lcDaemon) << "D-Bus service registered service=" << PhosphorProtocol::Service::Name
                     << "path=" << PhosphorProtocol::Service::ObjectPath;

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
            [this](const QString& zoneId, const ZoneGeometryRect& geometry) {
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

    connectScreenSignals();
    connectDesktopActivity();

    // Register global shortcuts via ShortcutManager.
    // setDefaultShortcut stores defaults synchronously (fast, no key grabbing),
    // then key grabs are activated via async D-Bus calls so the event loop
    // stays responsive for Wayland protocol events during login.
    m_shortcutManager->registerShortcuts();
    connectShortcutSignals();
    initializeAutotile();
    initializeUnifiedController();
    connectLayoutSignals();
    connectOverlaySignals();

    // Initial layout resolution: set the active layout from per-desktop assignments.
    // Must run after connectLayoutSignals() (which sets up autotile screens and filter)
    // and after connectDesktopActivity() (which sets current desktop/activity).
    // VirtualDesktopManager and ActivityManager no longer resolve layouts — this is
    // the single code path that understands autotile vs snapping mode.
    syncModeFromAssignments();

    finalizeStartup();

    // Migrate window screen assignments from physical to virtual IDs.
    // Must run AFTER finalizeStartup() which loads WTA state — otherwise
    // the migration finds no windows to migrate.
    migrateStartupScreenAssignments();

    m_running = true;
    // NOTE: daemonReady() is emitted by finalizeStartup() — do NOT emit again here.
}

void Daemon::stop()
{
    if (!m_running) {
        return;
    }

    // Stop pending timers to prevent callbacks during shutdown
    m_geometryUpdateTimer.stop();
    m_geometryUpdatePending = false;

    // Disconnect scripted algorithm loader to prevent file watcher events during teardown
    if (m_scriptedAlgorithmLoader) {
        m_scriptedAlgorithmLoader->disconnect();
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

    // ModeTracker delegates to LayoutManager's KConfig — no separate save needed

    m_reapplyGeometriesTimer.stop();

    // Autotile tiling state is now included in WTA's saveStateOnShutdown() above
    // via the tiling state serialization delegates. No separate save needed.
    //
    // Do NOT call setAutotileScreens({}) here — it emits windowsReleased
    // which clears WTS floating state and restarts the save timer, potentially
    // overwriting the correct WTS state saved above. The engine is destroyed
    // immediately after, so cleanup is unnecessary.

    // Clear adaptor engine pointers BEFORE destroying the engines.
    // Adaptors are Qt children of the daemon (destroyed later); a D-Bus call
    // arriving between engine destruction and adaptor destruction would otherwise
    // access freed memory. After clearing, ensureEngine() returns false.
    if (m_autotileAdaptor) {
        m_autotileAdaptor->clearEngine();
    }
    if (m_snapAdaptor) {
        m_snapAdaptor->clearEngine();
    }

    // Null the WindowDragAdaptor's engine pointer for the same reason.
    // Also null its ShortcutRegistrar pointer: m_shortcutManager (a unique_ptr
    // member) is destroyed before ~QObject runs, so it dies before the
    // adaptor itself. Any late event reaching the adaptor between those two
    // moments would otherwise deref a dead ShortcutManager.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setAutotileEngine(nullptr);
        m_windowDragAdaptor->setShortcutRegistrar(nullptr);
    }

    // Clear engine references before destruction
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setEngines(nullptr, nullptr);
    }

    // Null out the router's reference before destroying it — straggler calls
    // to engineForScreen() during the shutdown window get nullptr instead of
    // a dangling pointer. Then destroy the router.
    m_screenModeRouter.reset();

    // Destroy engines now (during stop(), before Qt child destruction order).
    m_snapEngine.reset();
    m_autotileEngine.reset();

    // Unregister D-Bus object path and service to prevent late calls during shutdown
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QString(PhosphorProtocol::Service::ObjectPath));
    bus.unregisterService(QString(PhosphorProtocol::Service::Name));

    // Sever the remaining raw-pointer adaptors from the unique_ptr members
    // they borrow. ~QObject destroys these adaptors AFTER all unique_ptr
    // members have already run their destructors, so without detach the
    // adaptors would see dangling pointers during the destruction window —
    // and the SettingsAdaptor dtor's save-on-teardown would deref a freed
    // Settings object. Each adaptor's detach() is null-safe + idempotent.
    //
    // WHY ONLY THESE THREE: SettingsAdaptor has the confirmed dtor-UAF
    // (debounced save timer flush). ShaderAdaptor + ControlAdaptor have
    // non-trivial signal wiring + cached state that benefits from
    // explicit teardown for the same "queued D-Bus call lands during
    // destruction window" defense-in-depth.
    //
    // The other eight raw-Qt-parented adaptors (LayoutAdaptor,
    // OverlayAdaptor, ZoneDetectionAdaptor, WindowTrackingAdaptor,
    // ScreenAdaptor, WindowDragAdaptor, SnapAdaptor, AutotileAdaptor) all
    // ship `= default` destructors (verified — see their class headers),
    // so they have no dtor body to UAF. QDBusConnection::unregisterObject
    // (invoked above) blocks new method dispatch to them before we begin
    // tearing down, and Qt's sender-destruction auto-disconnect cleans
    // up signal wiring when the borrowed sender (m_layoutManager, etc.)
    // is destroyed during member destruction. Adding detach() to those
    // eight would require null-guarding every slot body (they currently
    // rely on the "borrowed pointer is always valid" invariant), which
    // is a larger refactor than the defense-in-depth buys. If a future
    // adaptor grows a dtor body that derefs a borrowed member, add
    // detach() to it AND wire the call here — same pattern as these three.
    if (m_settingsAdaptor) {
        m_settingsAdaptor->detach();
    }
    if (m_shaderAdaptor) {
        m_shaderAdaptor->detach();
    }
    if (m_controlAdaptor) {
        m_controlAdaptor->detach();
    }

    // Drop the default-layout-id provider before member destruction. The
    // lambda installed in init() captures `this` and reads m_settings,
    // which is declared AFTER m_layoutManager and therefore destroyed
    // FIRST in reverse-order member teardown. No normal destruction path
    // calls defaultLayout() today, but clearing the capture makes that
    // latent ordering invariant unnecessary — future refactors that
    // trigger defaultLayout() from inside ~LayoutRegistry (e.g. signal
    // fan-out during qDeleteAll(m_layouts)) stay safe.
    if (m_layoutManager) {
        m_layoutManager->setDefaultLayoutIdProvider({});
    }

    m_running = false;
}

} // namespace PlasmaZones
