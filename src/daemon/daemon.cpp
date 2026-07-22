// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon.h"

#include <QGuiApplication>
#include <QFutureWatcher>
#include <QPointer>
#include <QStandardPaths>
#include <QtConcurrent>
#include <QScreen>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusError>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <PhosphorServiceIdle/IdleService.h>

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/QtQuickClockManager.h>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorSurface/SurfaceShaderRegistry.h>

#include <array>

#include "overlayservice.h"
#include "controllers/unifiedlayoutcontroller.h"
#include "controllers/shortcutmanager.h"
#include "rendering/surfaceshaderitem.h"
#include "rendering/zoneentryscaffold.h"
#include "rendering/zoneshadernoderhi.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSource.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/types/baselinecleanup.h"
#include "core/types/constants.h"
#include "core/resolve/crosssurfaceresolver.h"
#include "core/utils/geometryutils.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include "core/platform/logging.h"
#include "core/resolve/animationbootstrap.h"
#include "core/resolve/screenmoderouter.h"
#include "controllers/contextresolverwiring.h"

#include <PhosphorContext/ContextResolver.h>
#include "core/utils/utils.h"
#include "phosphor_i18n.h"
#include "config/configdefaults.h"
#include "config/settingsconfigstore.h"
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include "core/interfaces/shaderregistry.h"
#include "config/settings.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "dbus/settingsadaptor/settingsadaptor.h"
#include "dbus/overlayadaptor.h"
#include "dbus/zonedetectionadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/autotileadaptor/autotileadaptor.h"
#include "dbus/snapadaptor/snapadaptor.h"
#include "dbus/shaderadaptor.h"
#include "dbus/compositorbridgeadaptor.h"
#include "dbus/controladaptor.h"
#include "dbus/ruleadaptor.h"
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>

#include "controllers/enginefactory.h"
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "common/screenidresolver.h"
#include "common/layoutbundlebuilder.h"

namespace PlasmaZones {

// Snapping and autotile share one unified gap model: a single innerGap (the
// gap between two adjacent regions — each zone's interior edge insets by
// innerGap/2 so two neighbours leave an innerGap gap, see
// GeometryUtils::applyGapsToZoneGeometry) and a single outerGap (insets the
// content area from the screen edge per side), with one default for each.
// A context gap-override rule must therefore clamp to the same range in both
// modes; these assertions fail the build if the two ranges ever drift apart.
static_assert(Defaults::MaxGap == PhosphorTiles::AutotileDefaults::MaxGap,
              "Snapping and autotile gap clamp ceilings must match — they are the same gap quantity");

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
    // Unified Rule store — loads rules.json (written by the v3→v4
    // migration). Daemon is the sole writer; the RuleAdaptor exposes it.
    // Declared/constructed before m_layoutManager so the registry can borrow it.
    , m_ruleStore(std::make_unique<PhosphorRules::RuleStore>(ConfigDefaults::rulesFilePath()))
    , m_layoutManager(
          std::make_unique<PhosphorZones::LayoutRegistry>(m_ruleStore.get(), QStringLiteral("plasmazones/layouts")))
    , m_layoutComputeService(std::make_unique<PhosphorZones::LayoutComputeService>(nullptr))
    // m_curveRegistry / m_profileRegistry are default-constructed (no
    // init-list entries) — daemon.h declares them between m_layoutComputeService
    // and m_clockManager, so the in-class default initialisation runs before
    // any consumer below references them. m_clockManager owns its
    // QtQuickClockManager via unique_ptr; the manager itself is published
    // as the QML-side default in setupAnimationProfiles() below alongside
    // the profile registry.
    , m_clockManager(std::make_unique<PhosphorAnimation::QtQuickClockManager>())
    // Pass &m_curveRegistry into Settings so its initial load() resolves
    // Profile blobs through the daemon-owned registry. Requires
    // m_curveRegistry to be declared BEFORE m_settings in daemon.h —
    // see the DECLARATION ORDER INVARIANT comment there.
    //
    // Pass m_ruleStore.get() so Settings shares the daemon's single
    // canonical store rather than constructing a second one over the same
    // file. Two stores pointed at rules.json race on disk: each
    // mutator rebuilds its `kept` list from its own stale in-memory
    // snapshot, so the second writer silently drops rules the first writer
    // added. Mirrors the existing LayoutRegistry-via-borrowed-pointer
    // pattern above. Standalone settings / editor processes that have no
    // daemon-owned store pass nullptr and Settings falls back to owning
    // its own.
    , m_settings(std::make_unique<Settings>(m_configBackend.get(), &m_curveRegistry, m_ruleStore.get(), nullptr))
    , m_zoneDetector(std::make_unique<PhosphorZones::ZoneDetector>(nullptr))
    , m_windowRegistry(std::make_unique<PhosphorEngine::WindowRegistry>(nullptr))
    , m_panelSource(std::make_unique<PhosphorScreens::PlasmaPanelSource>())
    , m_virtualScreenStore(std::make_unique<SettingsConfigStore>(m_settings.get()))
    , m_screenManager(std::make_unique<PhosphorScreens::ScreenManager>(
          PhosphorScreens::ScreenManager::Config{
              .panelSource = m_panelSource.get(),
              .configStore = m_virtualScreenStore.get(),
              .useGeometrySensors = true,
              // Align the lib's cap with the daemon's source-of-truth (Settings
              // uses ConfigDefaults::maxVirtualScreensPerPhysical() when
              // validating writes). A lower cap here would silently reject
              // configs Settings accepted, leaving Settings ↔ PhosphorScreens::ScreenManager
              // divergent.
              .maxVirtualScreensPerPhysical = ConfigDefaults::maxVirtualScreensPerPhysical(),
          },
          nullptr))
    , m_shaderRegistry(std::make_unique<ShaderRegistry>(nullptr))
    , m_overlayService(
          std::make_unique<OverlayService>(m_screenManager.get(), m_shaderRegistry.get(), &m_profileRegistry, nullptr))
    , m_virtualDesktopManager(std::make_unique<PhosphorWorkspaces::VirtualDesktopManager>(nullptr))
    , m_activityManager(std::make_unique<PhosphorWorkspaces::ActivityManager>(nullptr))
    , m_shortcutManager(std::make_unique<ShortcutManager>(m_settings.get(), m_layoutManager.get(), nullptr))
{
    // Install the layout screen-id resolver before any Daemon-owned machinery
    // starts loading layouts. First-call ensures the once-only install runs
    // exactly once across all Daemon constructions in the process; subsequent
    // Daemons share the already-installed resolver. Moved out of the
    // `QObject((ensureScreenIdResolver(), parent))` comma-operator trick
    // because that idiom reads as an accidental typo.
    ensureScreenIdResolver();

    // The daemon no longer seeds the managed baseline appearance rules (borders,
    // title bars, gaps): those defaults now live in the config store. Strip any
    // stale copies a previous branch build wrote to rules.json so a carried-over
    // file does not keep three orphaned managed rules alive. Match only the fixed
    // baseline ids AND managed==true — a user's own rule is never touched. Rebuild
    // the list once via setAllRules (a single persist + rulesChanged) instead of
    // per-rule removeRule, and gate it on removedAny so a clean store (second
    // startup, or a fresh install) neither persists nor emits. The store loaded in
    // its constructor. Settings IS already a rulesChanged consumer (it subscribes
    // in its own ctor, constructed above), so a strip here reaches
    // Settings::onRuleStoreChanged; that only recomputes its gap fingerprint and
    // its downstream settingsChanged / perScreen* signals have no connected
    // listeners yet (the adaptors and geometry wiring land later in setup), so the
    // second-order effect is inert.
    if (m_ruleStore && !stripStaleManagedAppearanceBaselines(*m_ruleStore)) {
        qCWarning(lcDaemon) << "Failed to persist rules.json after stripping stale baseline appearance rules";
    }

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
    // project_plugin_based_compositor.md). The bundled algorithms ship as Luau
    // scripts and are loaded (with user scripts) by ScriptedAlgorithmLoader
    // during init().
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

    // Wire Settings::animationProfile into PhosphorProfileRegistry so
    // QML `PhosphorMotionAnimation { profile: … }` resolves to the
    // user's active animation settings and live-updates on edit.
    // Also populates the registry from user-authored JSON files under
    // the `plasmazones/` XDG namespace (consumer-agnostic loader
    // delegates directory walking to phosphor-fsloader).
    //
    // setupAnimationProfiles() also wires the daemon-owned
    // CurveRegistry into PhosphorCurve's QML static helper. Publication
    // of the static registry pointer is deferred into that function so
    // every QML observer sees the curves-loaded and profiles-loaded
    // states land together, rather than the static going live against
    // an empty registry for the brief window before loaders run.
    setupAnimationProfiles();
    setupAnimationShaderEffects();
    setupSurfaceShaderEffects();
}

Daemon::~Daemon()
{
    stop();
}

bool Daemon::init()
{
    // Settings constructor already calls load(); avoid duplicate load

    // init() decomposes into ordered phase methods (definitions split across
    // daemon/shader_warmup.cpp, daemon/init_services.cpp, daemon/init_adaptors.cpp,
    // daemon/init_engines.cpp). The call order below is byte-order faithful to the
    // former monolithic init() and MUST NOT be reordered — later phases borrow
    // members the earlier phases construct and wire.
    setupShaderWarmBakes();
    initLayoutAndSettingsWiring();
    initCoreAdaptors();
    initEnginesAndWiring();
    return registerDBusService();
}

} // namespace PlasmaZones
