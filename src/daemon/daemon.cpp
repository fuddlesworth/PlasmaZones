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
#include "../core/layoutmanager.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../core/layoutworker/layoutcomputeservice.h"
#include <PhosphorZones/ZoneDetector.h>
#include "../core/windowregistry.h"
#include "../core/screenmanager.h"
#include "../core/virtualdesktopmanager.h"
#include "../core/activitymanager.h"
#include "../core/constants.h"
#include "../core/geometryutils.h"
#include "../core/logging.h"
#include "../core/screenmoderouter.h"
#include "../core/utils.h"
#include "../core/virtualscreenswapper.h"
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
#include "../autotile/AutotileEngine.h"
#include "../autotile/autotilenavigationadapter.h"
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "../snap/SnapEngine.h"
#include "../snap/snapnavigationadapter.h"

namespace PlasmaZones {

namespace {
// Debounce interval (ms): coalesce rapid geometry changes (multi-screen, panel editor) into one update.
// Conceptually distinct from DELAYED_PANEL_REQUERY_MS in autotile.cpp (which schedules a
// follow-up panel geometry requery after the debounced update completes).
constexpr int GEOMETRY_UPDATE_DEBOUNCE_MS = 400;
} // anonymous namespace

namespace {
// Install the library-level screen-id resolver before any layouts load so
// fromJson() can normalise legacy connector names ("DP-2") to the daemon's
// EDID-based IDs ("LG:Model:Serial") during load. Installed on first Daemon
// construction (a static local ensures it runs exactly once).
struct InstallScreenIdResolver
{
    InstallScreenIdResolver()
    {
        PhosphorZones::Layout::setScreenIdResolver([](const QString& name) -> QString {
            if (name.isEmpty() || !Utils::isConnectorName(name))
                return name;
            return Utils::screenIdForName(name);
        });
    }
};
void ensureScreenIdResolver()
{
    static InstallScreenIdResolver s_installer;
    (void)s_installer;
}
} // namespace

Daemon::Daemon(QObject* parent)
    : QObject((ensureScreenIdResolver(), parent))
    // Don't pass 'this' as parent for unique_ptr-managed objects.
    // unique_ptr owns lifetime; a Qt parent would double-free.
    , m_configBackend(createDefaultConfigBackend())
    , m_layoutManager(std::make_unique<LayoutManager>(nullptr))
    , m_layoutComputeService(std::make_unique<LayoutComputeService>(nullptr))
    , m_settings(std::make_unique<Settings>(m_configBackend.get(), nullptr))
    , m_zoneDetector(std::make_unique<PhosphorZones::ZoneDetector>(nullptr))
    , m_windowRegistry(std::make_unique<WindowRegistry>(nullptr))
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

    // Wire PhosphorZones::ZoneDetector's adjacency threshold to the settings value. The
    // detector no longer holds an ISettings pointer (it takes just the int)
    // so we mirror the setting here and re-push on change.
    m_zoneDetector->setAdjacentThreshold(m_settings->adjacentThreshold());
    connect(m_settings.get(), &ISettings::adjacentThresholdChanged, this, [this]() {
        m_zoneDetector->setAdjacentThreshold(m_settings->adjacentThreshold());
    });

    // Build the layout sources here (rather than later in init()) because they
    // are thin wrappers — no I/O, no signal hookup — and consumers can ask for
    // layoutSource() any time after Daemon is constructed.  Population happens
    // lazily on first availableLayouts() call: the layout manager has loaded
    // from disk by then, and the algorithm registry is populated by the
    // PhosphorTiles::ScriptedAlgorithmLoader during init().
    m_zonesLayoutSource = std::make_unique<PhosphorZones::ZonesLayoutSource>(m_layoutManager.get());
    m_autotileLayoutSource =
        std::make_unique<PhosphorTiles::AutotileLayoutSource>(PhosphorTiles::AlgorithmRegistry::instance());
    m_layoutSource = std::make_unique<PhosphorLayout::CompositeLayoutSource>();
    m_layoutSource->addSource(m_zonesLayoutSource.get());
    m_layoutSource->addSource(m_autotileLayoutSource.get());
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

    // Initialize shader registry singleton (must be done early, before D-Bus adaptors)
    // The registry checks for Qt6::ShaderTools availability at compile time
    // and for qsb tool availability at runtime
    auto* shaderRegistry = new ShaderRegistry(this);
    auto scheduleWarmForShader =
        [this, registryPtr = QPointer<ShaderRegistry>(shaderRegistry)](const ShaderRegistry::ShaderInfo& info) {
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
            LayoutComputeService::recalculateSync(layout, GeometryUtils::effectiveScreenGeometry(layout, primary));
        }
    }

    // Configure overlay service with settings, layout manager, and default layout
    m_overlayService->setSettings(m_settings.get());
    m_overlayService->setLayoutManager(m_layoutManager.get());
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
    connect(m_layoutManager.get(), &LayoutManager::activeLayoutChanged, this, [this](PhosphorZones::Layout* layout) {
        if (layout) {
            // Recalculate zone geometries asynchronously using primary screen geometry.
            // Active layout is global; recalculating per-screen overwrites each
            // iteration (last-wins bug). The overlay computes per-screen geometry
            // on the fly via GeometryUtils::getZoneGeometryWithGaps().
            QScreen* primary = Utils::primaryScreen();
            if (primary) {
                QString screenId = Utils::screenIdentifier(primary);
                m_layoutComputeService->requestRecalculate(layout, screenId,
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
                    m_layoutComputeService->requestRecalculate(layout, screenId,
                                                               GeometryUtils::effectiveScreenGeometry(layout, screen));
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
        const auto prevPreviewParams = PhosphorTiles::AlgorithmRegistry::configuredPreviewParams();

        // Sync engine config (idempotent — skips retile if nothing changed)
        if (m_autotileEngine) {
            m_autotileEngine->syncFromSettings(m_settings.get());
        }

        // If tiling preview parameters changed (maxWindows, masterCount, splitRatio),
        // notify layout list consumers to refetch with updated previews
        if (PhosphorTiles::AlgorithmRegistry::configuredPreviewParams() != prevPreviewParams && m_layoutAdaptor) {
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
        // windowsReleasedFromTiling clears floating state for released windows.
        updateAutotileScreens();
        updateLayoutFilter();

        // Resnap after autotile disabled: restore windows to their pre-autotile
        // zone positions. PhosphorZones::Zone assignments are preserved during autotile (onLayoutChanged
        // skips autotile screens) so resnap uses original snap assignments.
        if (autotileToggled && !autotileNow && m_windowTrackingAdaptor) {
            m_suppressResnapOsd = 1;
            m_windowTrackingAdaptor->resnapCurrentAssignments();
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
    m_layoutAdaptor = new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), this);
    m_layoutAdaptor->setActivityManager(m_activityManager.get());
    m_layoutAdaptor->setSettings(m_settings.get());
    m_layoutAdaptor->setLayoutSource(m_layoutSource.get());
    // Invalidate D-Bus getActiveLayout() cache when the default layout changes in settings
    connect(m_settings.get(), &Settings::defaultLayoutIdChanged, m_layoutAdaptor, &LayoutAdaptor::invalidateCache);
    m_settingsAdaptor = new SettingsAdaptor(m_settings.get(), this);

    // Shader adaptor - shader discovery, compilation lifecycle, file monitoring
    new ShaderAdaptor(ShaderRegistry::instance(), this);

    // Compositor bridge adaptor - compositor-agnostic window control protocol.
    // Captured as a local so we can wire its bridgeRegistered signal to
    // WindowDragAdaptor::resetDragState below. Ownership stays with `this`
    // via QObject parent (passed to constructor).
    auto* compositorBridge = new CompositorBridgeAdaptor(this);

    // Overlay adaptor - overlay visibility and highlighting
    m_overlayAdaptor =
        new OverlayAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(), m_settings.get(), this);

    // PhosphorZones::Zone detection adaptor - zone detection queries
    m_zoneDetectionAdaptor =
        new ZoneDetectionAdaptor(m_zoneDetector.get(), m_layoutManager.get(), m_settings.get(), this);

    // Window tracking adaptor - window-zone assignments
    m_windowTrackingAdaptor = new WindowTrackingAdaptor(m_layoutManager.get(), m_zoneDetector.get(), m_settings.get(),
                                                        m_virtualDesktopManager.get(), this);
    m_windowTrackingAdaptor->setZoneDetectionAdaptor(m_zoneDetectionAdaptor);
    m_windowTrackingAdaptor->setWindowRegistry(m_windowRegistry.get());

    // Reapply window geometries after each geometry batch (processPendingGeometryUpdates).
    // When the delayed panel requery completes it emits availableGeometryChanged, which triggers
    // the same debounce → processPendingGeometryUpdates → reapply path; no separate delay needed.
    m_reapplyGeometriesTimer.setSingleShot(true);
    connect(&m_reapplyGeometriesTimer, &QTimer::timeout, m_windowTrackingAdaptor,
            &WindowTrackingAdaptor::requestReapplyWindowGeometries);

    m_screenAdaptor = new ScreenAdaptor(this);
    // ScreenAdaptor::setVirtualScreenConfig writes to Settings (the source
    // of truth); the daemon's Settings → ScreenManager observer wiring then
    // refreshes ScreenManager's cache and fires the downstream signal chain.
    m_screenAdaptor->setSettings(m_settings.get());

    // Window drag adaptor - handles drag events from KWin script
    // All drag logic (modifiers, zones, snapping) handled here
    m_windowDragAdaptor = new WindowDragAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                                m_settings.get(), m_windowTrackingAdaptor, this);

    // PhosphorZones::Zone selector methods are called directly from WindowDragAdaptor; QDBusAbstractAdaptor
    // signals are for D-Bus, not Qt connections.

    // Give the window drag adaptor access to the shortcut backend for
    // registering/unregistering the Escape cancel shortcut during drags
    m_windowDragAdaptor->setShortcutBackend(m_shortcutManager->shortcutBackend());

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

    // Initialize autotile engine
    m_autotileEngine = std::make_unique<AutotileEngine>(m_layoutManager.get(), m_windowTrackingAdaptor->service(),
                                                        m_screenManager.get(), this);
    // Attach the shared window registry so the engine queries current class
    // instead of parsing canonical windowIds — fixes Emby-style mid-session
    // class mutations (discussion #271).
    m_autotileEngine->setWindowRegistry(m_windowRegistry.get());

    // Initialize scripted algorithm loader BEFORE syncFromSettings so that
    // user-defined algorithms are registered in PhosphorTiles::AlgorithmRegistry before the
    // engine resolves the configured algorithm ID.
    m_scriptedAlgorithmLoader =
        std::make_unique<PhosphorTiles::ScriptedAlgorithmLoader>(QStringLiteral("plasmazones/algorithms"));
    // When scripted algorithms change (hot-reload), notify layout list consumers
    connect(m_scriptedAlgorithmLoader.get(), &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged, this,
            [this]() {
                if (m_layoutAdaptor)
                    m_layoutAdaptor->notifyLayoutListChanged();
            });
    m_scriptedAlgorithmLoader->scanAndRegister();

    m_autotileEngine->syncFromSettings(m_settings.get());
    m_autotileEngine->connectToSettings(m_settings.get());

    // Give the window drag adaptor access to the autotile engine for per-screen
    // autotile checks (overlay suppression and snap rejection on autotile screens)
    m_windowDragAdaptor->setAutotileEngine(m_autotileEngine.get());

    // Initialize SnapEngine for manual zone-based snapping
    m_snapEngine =
        std::make_unique<SnapEngine>(m_layoutManager.get(), m_windowTrackingAdaptor->service(), m_zoneDetector.get(),
                                     m_settings.get(), m_virtualDesktopManager.get(), this);

    // Wire persistence delegate — SnapEngine delegates save/load to WTA's KConfig layer.
    // QPointer guards against late calls during shutdown if WTA is destroyed first.
    m_snapEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
        });

    // Wire engine cross-references (SnapEngine ↔ AutotileEngine, zone detection).
    m_windowTrackingAdaptor->setEngines(m_snapEngine.get(), m_autotileEngine.get());

    // Wire SnapEngine's back-reference to the window tracking adaptor.
    // SnapEngine's navigation methods (focusInDirection, moveFocusedInDirection, …)
    // were moved out of WindowTrackingAdaptor and need to reach back into the
    // adaptor for shared state that hasn't been migrated yet: the target
    // resolver, the last-active window/screen shadow, and the snap-
    // bookkeeping helpers (windowSnapped, windowUnsnapped, recordSnapIntent,
    // clearPreTileGeometry). A future refactor should move that state onto
    // SnapEngine or WindowTrackingService and retire the back-reference.
    m_snapEngine->setWindowTrackingAdaptor(m_windowTrackingAdaptor);

    // Central routing table: single source of truth for "which engine owns
    // screen X". Every window-lifecycle / resnap / restore entry point in the
    // daemon and its adaptors consults this instead of scattering
    // isAutotileScreen() checks at each call site.
    m_screenModeRouter =
        std::make_unique<ScreenModeRouter>(m_layoutManager.get(), m_snapEngine.get(), m_autotileEngine.get());
    m_windowTrackingAdaptor->setScreenModeRouter(m_screenModeRouter.get());

    // Navigation-intent dispatch: thin INavigationActions adapters wired
    // through the router so daemon/navigation.cpp shortcut handlers dispatch
    // via router->navigatorFor(screenId)->foo() instead of branching on mode.
    // Both adapters forward to their respective engine — SnapEngine now
    // owns the snap-mode navigation methods that used to live on
    // WindowTrackingAdaptor (see src/snap/snapengine/navigation_actions.cpp).
    m_autotileNavigationAdapter = std::make_unique<AutotileNavigationAdapter>(m_autotileEngine.get());
    m_snapNavigationAdapter = std::make_unique<SnapNavigationAdapter>(m_snapEngine.get());
    m_screenModeRouter->setNavigationAdapters(m_snapNavigationAdapter.get(), m_autotileNavigationAdapter.get());

    // Stateless façade for VS swap/rotate. Held here so navigation handlers
    // and any future consumer share one instance instead of constructing
    // throwaway swappers per call. Constructed unconditionally during init
    // so downstream handlers (handleSwapVirtualScreen / handleRotateVirtualScreens)
    // can assume the pointer is non-null for the remainder of the daemon's lifetime.
    m_virtualScreenSwapper = std::make_unique<VirtualScreenSwapper>(m_settings.get());
    Q_ASSERT(m_virtualScreenSwapper);

    // Wire autotile persistence through WTA's KConfig layer (same delegate pattern as SnapEngine).
    // Note: engine->saveState() intentionally triggers a full WTA save (all window tracking
    // state, not just autotile). This is heavier than a targeted save but ensures consistency
    // — the autotile window orders are embedded in WTA's save cycle via the serialization
    // delegates below. The engine-level delegates exist to satisfy the IEngineLifecycle interface.
    // QPointer guards against late calls during shutdown if WTA is destroyed first.
    m_autotileEngine->setPersistenceDelegate(
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->saveState();
        },
        [wta = QPointer(m_windowTrackingAdaptor)]() {
            if (wta)
                wta->loadState();
        });
    m_autotileEngine->setIsWindowFloatingFn([wta = QPointer(m_windowTrackingAdaptor)](const QString& windowId) -> bool {
        return wta && wta->service() && wta->service()->isWindowFloating(windowId);
    });

    // Wire window order serialization delegates so WTA includes autotile window
    // orders in its save/load cycle (analogous to WindowZoneAssignmentsFull for snap mode)
    m_windowTrackingAdaptor->setTilingStateDelegates(
        [engine = QPointer(m_autotileEngine.get())]() -> QJsonArray {
            return engine ? engine->serializeWindowOrders() : QJsonArray{};
        },
        [engine = QPointer(m_autotileEngine.get())](const QJsonArray& orders) {
            if (engine)
                engine->deserializeWindowOrders(orders);
        });

    m_windowTrackingAdaptor->setTilingPendingRestoreDelegates(
        [engine = QPointer(m_autotileEngine.get())]() -> QJsonObject {
            return engine ? engine->serializePendingRestores() : QJsonObject{};
        },
        [engine = QPointer(m_autotileEngine.get())](const QJsonObject& obj) {
            if (engine)
                engine->deserializePendingRestores(obj);
        });

    // Trigger WTA save on autotile state changes (window order, split ratio, master count).
    // Narrower dirty mask than the default DirtyAll — only the two autotile-owned
    // fields can change as a result of a tilingChanged signal, so the next save
    // rewrites just those keys rather than the whole window-tracking blob.
    //
    // markDirty() emits WindowTrackingService::stateChanged, which is wired to
    // WindowTrackingAdaptor::scheduleSaveState in the adaptor's constructor —
    // that connection is what actually kicks the debounced save timer. If the
    // stateChanged hookup ever gets severed, autotile state will silently
    // stop persisting; add an explicit scheduleSaveState() call here if so.
    connect(m_autotileEngine.get(), &AutotileEngine::tilingChanged, m_windowTrackingAdaptor, [this]() {
        if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
            m_windowTrackingAdaptor->service()->markDirty(WindowTrackingService::DirtyAutotileOrders
                                                          | WindowTrackingService::DirtyAutotilePending);
        }
    });

    // Create engine D-Bus adaptors — each engine has a dedicated adaptor that
    // connects signals in its constructor (unified pattern for both engines)
    m_snapAdaptor = new SnapAdaptor(m_snapEngine.get(), m_windowTrackingAdaptor, this);
    m_autotileAdaptor = new AutotileAdaptor(m_autotileEngine.get(), this);

    // Control adaptor - high-level convenience API for third-party integrations
    new ControlAdaptor(m_windowTrackingAdaptor, m_layoutAdaptor, m_layoutManager.get(), m_autotileEngine.get(),
                       m_screenManager.get(), this);

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
            m_snapEngine->resnapToNewLayout();

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
        if (bus.registerService(QString(DBus::ServiceName))) {
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
        qCCritical(lcDaemon) << "Failed to register D-Bus service=" << DBus::ServiceName << "error=" << error.message()
                             << "type=" << error.type();
        return false;
    }

    if (!serviceRegistered) {
        qCCritical(lcDaemon) << "Failed to register D-Bus service after" << maxRetries << "attempts";
        return false;
    }

    // Register D-Bus object (no retry needed - service is already registered)
    if (!bus.registerObject(QString(DBus::ObjectPath), this)) {
        QDBusError error = bus.lastError();
        qCCritical(lcDaemon) << "Failed to register D-Bus object=" << DBus::ObjectPath << "error=" << error.message();
        // Cleanup: unregister service if object registration fails
        bus.unregisterService(QString(DBus::ServiceName));
        return false;
    }

    qCInfo(lcDaemon) << "D-Bus service registered service=" << DBus::ServiceName << "path=" << DBus::ObjectPath;

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
    // Do NOT call setAutotileScreens({}) here — it emits windowsReleasedFromTiling
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
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setAutotileEngine(nullptr);
    }

    // Clear engine references before destruction
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setEngines(nullptr, nullptr);
    }

    // Clear navigation adapters from the router and tear them down BEFORE
    // destroying the engines they point at. The adapters hold QPointers to
    // the engines (so a stray call would no-op rather than crash), but we
    // still prefer to sever the reference chain explicitly to match the
    // SnapAdaptor/AutotileAdaptor teardown pattern and keep the router from
    // handing out about-to-be-invalid navigators during the shutdown window.
    if (m_screenModeRouter) {
        m_screenModeRouter->setNavigationAdapters(nullptr, nullptr);
    }
    m_snapNavigationAdapter.reset();
    m_autotileNavigationAdapter.reset();

    // Destroy engines now (during stop(), before Qt child destruction order).
    m_snapEngine.reset();
    m_autotileEngine.reset();

    // Unregister D-Bus object path and service to prevent late calls during shutdown
    QDBusConnection bus = QDBusConnection::sessionBus();
    bus.unregisterObject(QString(DBus::ObjectPath));
    bus.unregisterService(QString(DBus::ServiceName));

    m_running = false;
}

} // namespace PlasmaZones
