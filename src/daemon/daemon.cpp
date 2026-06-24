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
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/QtQuickClockManager.h>

#include <PhosphorAnimation/AnimationShaderRegistry.h>

#include <array>

#include "overlayservice.h"
#include "unifiedlayoutcontroller.h"
#include "modetracker.h"
#include "unifiedlayoutcontroller.h"
#include "shortcutmanager.h"
#include "rendering/zoneentryscaffold.h"
#include "rendering/zoneshadernoderhi.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "../config/configbackends.h"
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
#include "../core/constants.h"
#include "../core/crosssurfaceresolver.h"
#include "../core/geometryutils.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include "../core/logging.h"
#include "../core/animationbootstrap.h"
#include "../core/screenmoderouter.h"
#include "contextresolverwiring.h"

#include <PhosphorContext/ContextResolver.h>
#include "../core/utils.h"
#include "../phosphor_i18n.h"
#include "../config/configdefaults.h"
#include "../config/settingsconfigstore.h"
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include "../core/shaderregistry.h"
#include "../config/settings.h"
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
#include "../dbus/controladaptor.h"
#include "../dbus/windowruleadaptor.h"
#include <PhosphorWindowRules/ExclusionRules.h>
#include <PhosphorWindowRules/WindowRuleStore.h>
#include "enginefactory.h"
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include "../common/screenidresolver.h"
#include "../common/layoutbundlebuilder.h"

namespace PlasmaZones {

// Snapping and autotile gaps are the SAME two quantities under different names:
// snapping's per-zone "zonePadding" is the full inter-region gap (each zone's
// interior edge insets by zonePadding/2, so two neighbours leave a zonePadding
// gap — see GeometryUtils::applyGapsToZoneGeometry), exactly like autotile's
// "innerGap" (the single gap between two tiles). Snapping "outerGap" and
// autotile "outerGap" both inset the content area from the screen edge per side.
// A context gap-override rule must therefore clamp to the same range in both
// modes; these assertions fail the build if the two ranges ever drift apart.
// (The defaults may legitimately differ per mode, so only the range is tied.)
static_assert(Defaults::MaxGap == PhosphorTiles::AutotileDefaults::MaxGap,
              "Snapping and autotile gap clamp ceilings must match — they are the same gap quantity");

namespace {
// Debounce interval (ms): coalesce rapid geometry changes (multi-screen, panel editor) into one update.
// Conceptually distinct from DELAYED_PANEL_REQUERY_MS in autotile.cpp (which schedules a
// follow-up panel geometry requery after the debounced update completes).
constexpr int GEOMETRY_UPDATE_DEBOUNCE_MS = 400;

// Grace period (ms) for the KWin effect to register as a compositor bridge
// after daemon startup. Comfortably longer than a healthy effect takes to
// register (sub-second once KWin and the daemon's D-Bus name are both up),
// even when the daemon starts before KWin during login — so a timeout means
// a genuine failure, not a race.
constexpr int BRIDGE_WATCHDOG_TIMEOUT_MS = 20000;

// Locate the installed PlasmaZones KWin effect plugin and read the KWin
// version embedded in its plugin interface ID. The KWin effect is a compiled
// C++ plugin; KWin bakes its exact version into the IID it accepts
// (EffectPluginFactory_iid = "org.kde.kwin.EffectPluginFactory" + the KWin
// version string), and silently rejects any plugin built against a different
// KWin. metaData() reads only the static metadata section — no dlopen — so it
// works even on the version-mismatched plugin KWin itself refuses to load.
// `installed` is set to whether the plugin file was found at all. Returns the
// KWin version the effect was built against, or empty when the plugin is
// missing or its IID is not a recognizable KWin effect IID.
QString probeEffectKWinVersion(bool& installed)
{
    static const QLatin1String iidPrefix("org.kde.kwin.EffectPluginFactory");
    const QString effectRelPath = QStringLiteral("kwin/effects/plugins/kwin_effect_plasmazones.so");

    installed = false;
    const QStringList libraryPaths = QCoreApplication::libraryPaths();
    for (const QString& base : libraryPaths) {
        const QString candidate = base + QLatin1Char('/') + effectRelPath;
        if (!QFile::exists(candidate)) {
            continue;
        }
        installed = true;
        const QString iid = QPluginLoader(candidate).metaData().value(QLatin1String("IID")).toString();
        return iid.startsWith(iidPrefix) ? iid.mid(iidPrefix.size()) : QString();
    }
    return QString();
}
} // anonymous namespace

Daemon::Daemon(QObject* parent)
    : QObject(parent)
    // Don't pass 'this' as parent for unique_ptr-managed objects.
    // unique_ptr owns lifetime; a Qt parent would double-free.
    , m_configBackend(createDefaultConfigBackend())
    // Unified WindowRule store — loads windowrules.json (written by the v3→v4
    // migration). Daemon is the sole writer; the WindowRuleAdaptor exposes it.
    // Declared/constructed before m_layoutManager so the registry can borrow it.
    , m_windowRuleStore(std::make_unique<PhosphorWindowRules::WindowRuleStore>(ConfigDefaults::windowRulesFilePath()))
    , m_layoutManager(std::make_unique<PhosphorZones::LayoutRegistry>(m_windowRuleStore.get(),
                                                                      QStringLiteral("plasmazones/layouts")))
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
    // Pass m_windowRuleStore.get() so Settings shares the daemon's single
    // canonical store rather than constructing a second one over the same
    // file. Two stores pointed at windowrules.json race on disk: each
    // mutator rebuilds its `kept` list from its own stale in-memory
    // snapshot, so the second writer silently drops rules the first writer
    // added. Mirrors the existing LayoutRegistry-via-borrowed-pointer
    // pattern above. Standalone settings / editor processes that have no
    // daemon-owned store pass nullptr and Settings falls back to owning
    // its own.
    , m_settings(std::make_unique<Settings>(m_configBackend.get(), &m_curveRegistry, m_windowRuleStore.get(), nullptr))
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
}

// Paths that follow the user's `Settings.animationProfile` slider
// directly. Every other PhosphorAnimation path is served by
// `${KDE_INSTALL_DATADIR}/plasmazones/profiles/<path>.json` (shipped
// defaults), with user overrides at
// `~/.local/share/plasmazones/profiles/<path>.json` — all discovered
// and merged by `ProfileLoader`.
//
// Keeping this list in a file-scope array lets us add another
// settings-backed path (e.g., a second slider for snap-specific
// feel) without touching the publish loop.
//
// `static const` rather than `constexpr`: the array stores pointers to
// `ProfilePaths::Global`, a non-`constexpr` QString. `constexpr` on a
// non-`constexpr` pointee compiles but misrepresents the guarantee — the
// pointer is a runtime address, not a constant expression. `static const`
// matches the actual lifetime (initialised-on-first-use global storage)
// without the misleading label.
static const auto kSettingsDrivenProfilePaths = std::array{
    &PhosphorAnimation::ProfilePaths::Global,
};

/// Owner tag used to partition every profile registered by the daemon's
/// ProfileLoader (user-authored JSON files under
/// `~/.local/share/plasmazones/profiles/`). Lives in the registry's
/// partitioned-ownership map so a `clearOwner` call on this tag wipes
/// only the user-JSON partition without touching settings-driven entries
/// (which are owned by the empty/direct tag) or any other consumer's
/// registrations.
static constexpr QLatin1StringView kPlasmaZonesUserProfilesOwnerTag{"plasmazones-user-profiles"};

void Daemon::setupAnimationProfiles()
{
    using namespace PhosphorAnimation;

    // Wipe any entries left over from prior wiring on this same daemon
    // instance. setupAnimationProfiles is called exactly once per Daemon
    // construction (from the ctor — not init()), so the registry is
    // always empty when we get here — the narrow-clear is a no-op in
    // current code paths.
    //
    // Narrow the clear to the two partitions we publish under: the
    // loader-owned user-JSON partition (clearOwner by tag) and each
    // individual settings-driven path (unregisterProfile per path).
    // Wholesale `clear()` would also evict any other consumer's
    // entries if they happened to register before us — not a concern
    // in production today but the narrower scope is the correct
    // contract for a registry that may be shared with other consumers.
    PhosphorProfileRegistry& registry = m_profileRegistry;
    registry.clearOwner(kPlasmaZonesUserProfilesOwnerTag);
    registry.clearOwner(QString(kShellAnimationFamilySeedsOwnerTag));
    for (const QString* path : kSettingsDrivenProfilePaths) {
        registry.unregisterProfile(*path);
    }

    // Configure the registry's two-layer resolveWithInheritance — seed
    // entries form the lowest-precedence layer so a user edit at any
    // depth still wins over any leaf seed. Idempotent across reload
    // paths; setting the same tag is a cheap no-op under the registry's
    // internal lock.
    registry.setLowPrecedenceOwnerTag(QString(kShellAnimationFamilySeedsOwnerTag));

    // Discover XDG `plasmazones/{curves,profiles}` dirs, materialise the
    // user-writable dirs, construct the loaders, and wire the
    // curveLoader→profileLoader rescan. Shared with the secondary
    // composition roots (settings / editor) via `AnimationBootstrap` —
    // both paths funnel through `constructAnimationLoaders` so the
    // dir-discovery and loader-construction logic only exists in one
    // place. The owner tag here is daemon-specific so the registry's
    // partitioned-ownership map keeps daemon-loaded user JSON entries
    // distinct from any secondary process's loader entries (today
    // they're separate processes, but the partitioning preserves the
    // contract).
    //
    // The initial `loadFromDirectories` scan is deferred until AFTER
    // the daemon's pre-scan signal wiring below — a loader's
    // initial-scan emit otherwise fires before the
    // publishActiveAnimationProfile listener is installed and is
    // silently dropped. Triggered explicitly via
    // `runInitialAnimationLoad` further down.
    auto loaderHandles =
        constructAnimationLoaders(m_curveRegistry, m_profileRegistry, kPlasmaZonesUserProfilesOwnerTag, nullptr);
    m_curveLoader = std::move(loaderHandles.curveLoader);
    m_profileLoader = std::move(loaderHandles.profileLoader);
    const AnimationLoaderDirs loaderDirs = std::move(loaderHandles.dirs);

    // Connect BEFORE the initial scans below so any signal Settings
    // fires during load (or any signal the ProfileLoader fires during
    // its own initial scan) is captured. The registry's value-changed
    // guard makes the subsequent publishActiveAnimationProfile a no-op
    // if the signal-driven path already published the same values.
    //
    // Re-publish on:
    //   - Settings edits (slider drag, per-field setter) — the aggregate
    //     animationProfileChanged signal fires.
    //   - ProfileLoader rescans — user added/removed a JSON file, which
    //     flips the hasProfile() check for some paths.
    //   - CurveLoader rescans — a curve JSON referenced by the
    //     settings-driven Global profile changed on disk. Settings
    //     ::animationProfile() reparses the stored blob through
    //     CurveRegistry on every call (no cache), so republishing
    //     re-resolves the curve against the fresh registry state.
    //     Without this wire, a curve edit is only visible to profiles
    //     loaded from JSON (via the curveLoader→profileLoader rescan
    //     above), NOT to the settings-fanout path — the Global slider's
    //     curve reference would silently go stale until the next
    //     Settings edit.
    // All three signals route through `requestAnimationProfilePublish`
    // — a coalescing trampoline that collapses every fan-in within the
    // same event-loop tick into exactly one `publishActiveAnimationProfile`
    // call. The settings-slider drag on its own fires the aggregate at
    // ~30 Hz, and a curve-pack edit can fire `curvesChanged` then
    // `profilesChanged` (via the `curveLoader → profileLoader` rescan
    // wire) within the same tick — without coalescing, the publish
    // path's Settings parse + curve resolve runs three times per tick
    // for one user action.
    m_animationPublishTimer.setSingleShot(true);
    m_animationPublishTimer.setInterval(0);
    connect(&m_animationPublishTimer, &QTimer::timeout, this, [this]() {
        m_animationPublishPending = false;
        publishActiveAnimationProfile();
    });
    connect(m_settings.get(), &Settings::animationProfileChanged, this, [this]() {
        requestAnimationProfilePublish();
    });
    connect(m_profileLoader.get(), &ProfileLoader::profilesChanged, this, [this]() {
        requestAnimationProfilePublish();
    });
    connect(m_curveLoader.get(), &CurveLoader::curvesChanged, this, [this]() {
        requestAnimationProfilePublish();
    });

    // Wire the daemon-owned CurveRegistry into the QML static helper so
    // every QML callsite that resolves curve wire-format strings uses
    // the same per-process registry. Moved from the Daemon ctor into
    // this function (between signal wiring and the initial scans) so
    // publication of the static and population of the registry land
    // together from QML's perspective — the static never goes live
    // against an empty registry for the brief window before loaders
    // run. The null-out in stop() prevents the static from dangling
    // across process-lifetime Daemon reconstruction (e.g. in tests).
    PhosphorCurve::setDefaultRegistry(&m_curveRegistry);

    // Publish the daemon-owned PhosphorProfileRegistry as the QML-side
    // default — every `PhosphorMotionAnimation { profile: "<path>" }`
    // in the overlay shell resolves through this pointer. Phase A3 of
    // the architecture refactor: replaces the prior
    // `PhosphorProfileRegistry::instance()` Meyers singleton with
    // explicit composition-root publication. Cleared in `stop()` before
    // the registry destructs.
    PhosphorProfileRegistry::setDefaultRegistry(&m_profileRegistry);

    // Publish the daemon-owned QtQuickClockManager as the QML-side
    // default — `PhosphorAnimatedValueBase::resolveClock` in any
    // `PhosphorAnimatedReal/Color/Point/Rect/Size` instance that the
    // overlay shell instantiates resolves through this pointer.
    // Cleared in `stop()` before the manager destructs.
    QtQuickClockManager::setDefaultManager(m_clockManager.get());

    // Three-phase initial load — curves first so the family-seed step
    // can resolve named curves like `widget-out`; family seeds next so
    // the profile loader's reloadFromOwner correctly overwrites a seed
    // when the user authored a JSON at the same path; profiles last.
    // The split mirrors AnimationBootstrap so secondary composition
    // roots get the same seeding shape.
    runInitialCurveLoad(*m_curveLoader, loaderDirs);
    seedShellAnimationFamilies(m_profileRegistry, m_curveRegistry);
    runInitialProfileLoad(*m_profileLoader, loaderDirs);

    // Final explicit publish covers the case where neither the Settings
    // nor the ProfileLoader emitted during the loads above (e.g. fresh
    // install with no user JSON, no settings edit during construction).
    // Partitioned-ownership in the registry ensures the loader's
    // user-files entries are not wiped by this direct-owner publish.
    publishActiveAnimationProfile();
}

void Daemon::requestAnimationProfilePublish()
{
    // Idempotent — if the trampoline is already pending, additional
    // signals in the same tick are absorbed for free.
    if (m_animationPublishPending) {
        return;
    }
    m_animationPublishPending = true;
    m_animationPublishTimer.start();
}

void Daemon::publishActiveAnimationProfile()
{
    using namespace PhosphorAnimation;

    // Publish the settings-driven paths (Global). Every OTHER path is
    // served by `ProfileLoader` from `plasmazones/profiles/*.json` —
    // shipped defaults live in `${KDE_INSTALL_DATADIR}/plasmazones/
    // profiles/`, user overrides in `~/.local/share/plasmazones/
    // profiles/`. `registerProfile` has an equality guard so
    // re-publishing identical values on every settingsChanged signal
    // is a cheap no-op on the hot path.
    //
    // User-wins at the registry level: if the ProfileLoader has a
    // user-authored JSON file at a settings-driven path, we skip the
    // direct publish so their owner-tagged entry wins. On JSON delete,
    // the loader emits profilesChanged, this function re-runs, and the
    // settings-default path is restored.
    //
    // This runs on the settings-slider hot path (~30 Hz during drag),
    // so O(1) `hasPath` is used instead of `entries()` which copies
    // and sorts the full tracked set on every tick.
    auto& reg = m_profileRegistry;

    const Profile settingsProfile = m_settings->animationProfile();
    for (const QString* path : kSettingsDrivenProfilePaths) {
        if (m_profileLoader && m_profileLoader->hasPath(*path)) {
            continue;
        }
        reg.registerProfile(*path, settingsProfile);
    }
}

void Daemon::setupAnimationShaderEffects()
{
    m_animationShaderRegistry = std::make_unique<PhosphorAnimationShaders::AnimationShaderRegistry>(nullptr);

    // System dirs from XDG_DATA_DIRS in descending priority. Reverse so
    // the first registered is the lowest-priority system dir — the
    // strategy reverse-iterates and applies first-registration-wins,
    // which yields the canonical XDG semantic
    // `user > sys-highest > ... > sys-lowest` after the user dir is
    // appended last.
    QStringList animDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/animations"), QStandardPaths::LocateDirectory);
    std::reverse(animDirs.begin(), animDirs.end());

    const QString userAnimDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/animations");
    if (!animDirs.contains(userAnimDir))
        animDirs.append(userAnimDir);

    // Materialise the user dir BEFORE registering so the watcher attaches
    // a direct watch instead of a parent-watch proxy. Without this, on a
    // fresh install (where `~/.local/share/plasmazones/animations` does
    // not yet exist) the watcher would climb to the user data root —
    // which is forbidden under the new fsloader rules — and silently
    // disable live-reload until the user manually triggered a refresh.
    // Mirrors the curve/profile/script setup pattern. Failures are non-
    // fatal — the on-demand scan still runs without a watch.
    QDir().mkpath(userAnimDir);

    m_animationShaderRegistry->setUserPath(userAnimDir);
    m_animationShaderRegistry->addSearchPaths(animDirs);

    if (m_overlayService) {
        m_overlayService->setAnimationShaderRegistry(m_animationShaderRegistry.get());
    }
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
            auto* watcher = new QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>(this);
            connect(watcher, &QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>::finished, this,
                    [registryPtr, watcher, shaderId]() {
                        if (!registryPtr) {
                            watcher->deleteLater();
                            return;
                        }
                        const PhosphorRendering::WarmShaderBakeResult r = watcher->result();
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
            // T1.4 + T1.1: warm-bake with the SAME entry-point scaffold AND the
            // SAME generated param preamble ZoneShaderItem installs at runtime,
            // so a zone pack's warm entry keys identically to its live load (both
            // are folded into the bake-cache key). Computed on the GUI thread
            // (reads `info`) and captured by value into the bake-thread lambda.
            const QString entryPrologue = zoneEntryPrologue();
            const QList<PhosphorShaders::EntryCandidate> entryCandidates = zoneEntryCandidates();
            const QString paramPreamble = ShaderRegistry::paramPreamble(info);
            watcher->setFuture(QtConcurrent::run(&m_shaderBakePool,
                                                 [vertPath = info.vertexShaderPath, fragPath = info.sourcePath,
                                                  includePaths, paramPreamble, entryPrologue, entryCandidates]() {
                                                     return warmShaderBakeCacheForPaths(vertPath, fragPath,
                                                                                        includePaths, paramPreamble,
                                                                                        entryPrologue, entryCandidates);
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

    // Warm-bake ANIMATION shaders (fly-in / dissolve / etc.) the same
    // way zone shaders are warmed above. Without this, the first OSD
    // show after a fresh daemon start hits a cold cache: QShaderBaker
    // compiles the animation shader synchronously on the render thread
    // during the first frame, and the AV's animation duration (e.g.
    // 500 ms) can elapse before the shader is ready to paint. The
    // observed symptom: card "pops in" at rest with no slide animation
    // on the first OSD; every subsequent OSD show animates correctly
    // because the cache is now warm.
    //
    // Shares m_shaderBakePool with the zone-shader warm-bake. The
    // pool is single-threaded (QShaderBaker / glslang isn't thread-
    // safe), so animation and zone bakes serialise without interfering.
    //
    // Include-path resolution mirrors `SurfaceAnimator::runLeg`
    // (surfaceanimator.cpp): every animation search-path's `/shared`
    // subdir is added so an effect's vert / frag can
    // `#include <animation_uniforms.glsl>` and the bake worker resolves
    // it identically to the render-thread load path. The vertex-path fallback (default
    // `shared/animation.vert` when an effect doesn't ship its own)
    // also mirrors the runtime, otherwise the warm-baked entry's
    // cache key would differ from what runtime queries.
    if (m_animationShaderRegistry) {
        auto scheduleWarmForAnimEffect = [this,
                                          registryPtr = QPointer<PhosphorAnimationShaders::AnimationShaderRegistry>(
                                              m_animationShaderRegistry.get())](
                                             const PhosphorAnimationShaders::AnimationShaderEffect& info) {
            if (!info.isValid() || info.fragmentShaderPath.isEmpty() || !QFile::exists(info.fragmentShaderPath)) {
                return;
            }
            PhosphorAnimationShaders::AnimationShaderRegistry* reg = registryPtr.data();
            if (!reg) {
                return;
            }
            QString vertPath = info.vertexShaderPath;
            QStringList includePaths;
            for (const QString& sp : reg->searchPaths()) {
                const QString sharedDir = sp + QStringLiteral("/shared");
                if (QDir(sharedDir).exists()) {
                    includePaths.append(sharedDir);
                    if (vertPath.isEmpty()) {
                        const QString sharedVert = sharedDir + QStringLiteral("/animation.vert");
                        if (QFile::exists(sharedVert)) {
                            vertPath = sharedVert;
                        }
                    }
                }
            }
            if (vertPath.isEmpty() || !QFile::exists(vertPath)) {
                return;
            }
            const QString effectId = info.id;
            // T1.1: warm-bake with the SAME generated preamble SurfaceAnimator
            // splices at runtime, so the warm entry's cache key (fingerprinted
            // on the preamble) matches the live load — otherwise the live bake
            // would miss this entry, or worse, a key collision would serve the
            // wrong SPIR-V. Computed on the GUI thread (reads `info`) and
            // captured by value into the bake-thread lambda.
            const QString paramPreamble = PhosphorAnimationShaders::AnimationShaderRegistry::paramPreamble(info);
            // T1.5: warm-bake with the same entry-point scaffold SurfaceAnimator
            // installs at runtime, so an entry-only animation pack's warm entry
            // keys identically to its live load. Inert for every traditional
            // pack (which assembles to itself).
            const QString entryPrologue = PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryPrologue();
            const QList<PhosphorShaders::EntryCandidate> entryCandidates =
                PhosphorAnimationShaders::AnimationShaderRegistry::animationEntryCandidates();
            auto* watcher = new QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>(this);
            connect(watcher, &QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>::finished, this,
                    [watcher, effectId]() {
                        const PhosphorRendering::WarmShaderBakeResult r = watcher->result();
                        if (!r.success) {
                            qCWarning(lcDaemon) << "Animation shader bake: failed for" << effectId << r.errorMessage;
                        }
                        watcher->deleteLater();
                    });
            watcher->setFuture(QtConcurrent::run(&m_shaderBakePool,
                                                 [vertPath, fragPath = info.fragmentShaderPath, includePaths,
                                                  paramPreamble, entryPrologue, entryCandidates]() {
                                                     return warmShaderBakeCacheForPaths(vertPath, fragPath,
                                                                                        includePaths, paramPreamble,
                                                                                        entryPrologue, entryCandidates);
                                                 }));
        };
        connect(m_animationShaderRegistry.get(), &PhosphorAnimationShaders::AnimationShaderRegistry::effectsChanged,
                this, [this, scheduleWarmForAnimEffect]() {
                    if (!m_animationShaderRegistry) {
                        return;
                    }
                    const QList<PhosphorAnimationShaders::AnimationShaderEffect> effects =
                        m_animationShaderRegistry->availableEffects();
                    for (const PhosphorAnimationShaders::AnimationShaderEffect& info : effects) {
                        scheduleWarmForAnimEffect(info);
                    }
                });
        for (const PhosphorAnimationShaders::AnimationShaderEffect& info :
             m_animationShaderRegistry->availableEffects()) {
            scheduleWarmForAnimEffect(info);
        }
    }

    // Wire the level-1 (global) cascade tier as two pass-through
    // providers — snap default layout id and autotile default algorithm
    // id — symmetric in shape and each gated on its own enabled flag.
    // The library decides precedence (snap > autotile when both are
    // non-empty); the daemon does not arbitrate mode here. When
    // snappingEnabled is false the snap provider returns empty, so
    // the cascade naturally resolves autotile defaults for unassigned
    // contexts (fixes #368 without baking engine specifics into the
    // composition root).
    //
    // Lifetime: m_settings is declared AFTER m_layoutManager in
    // daemon.h, so reverse-order member destruction tears m_settings
    // down FIRST. The lambdas capture `this` and dereference m_settings,
    // so any cascade query during member-destruction would UAF without
    // the explicit teardown in stop() (which clears both providers
    // before any unique_ptr member runs its destructor) plus the null
    // checks below as a belt-and-suspenders guard against future
    // refactors that reset m_settings explicitly. NOTE: snap with
    // defaultLayoutId="" silently falls through to the autotile branch
    // — see test_layoutmanager_assignment.cpp
    // testLevel1Default_snapEnabledEmptyId_autotileEnabled_autotileWins
    // for the pinned behaviour.
    m_layoutManager->setDefaultLayoutIdProvider([this]() {
        if (!m_settings || !m_settings->snappingEnabled()) {
            return QString();
        }
        return m_settings->defaultLayoutId();
    });
    m_layoutManager->setDefaultAutotileAlgorithmProvider([this]() {
        if (!m_settings || !m_settings->autotileEnabled()) {
            return QString();
        }
        return m_settings->defaultAutotileAlgorithm();
    });
    // Snapping-preferred provider — separate from defaultLayoutIdProvider
    // because the user can have snapping enabled WITHOUT a global default
    // snap layout id (per-screen assignments cover everything). Without
    // this signal the cascade would fall through to autotile when both
    // (snappingEnabled && defaultLayoutId == "") and (autotileEnabled &&
    // defaultAutotileAlgorithm != ""), surfacing "Tiling: Binary Split"
    // OSD content to a user who never enabled autotile globally.
    m_layoutManager->setSnappingPreferredProvider([this]() {
        return m_settings && m_settings->snappingEnabled();
    });
    // Global "suppress default layout assignment" gate. When on, the level-1
    // default synthesis above is short-circuited so an unassigned context gets
    // no active layout (no engine activates) until the user assigns one — the
    // same effective state as having no default providers configured. The
    // per-context DefaultLayoutAssignment window rule overrides this either way.
    m_layoutManager->setDefaultAssignmentSuppressedProvider([this]() {
        return m_settings && m_settings->suppressDefaultLayoutAssignment();
    });
    // Wire the compute service to the layout manager so tracked layouts
    // are evicted on removal (bounds m_trackedLayouts over time).
    m_layoutComputeService->setLayoutManager(m_layoutManager.get());

    // Seed the curated default picker visibility on a fresh install (no-op when
    // a layout-settings.json / autotile-overrides.json already exists), before
    // loadLayouts() so the seeded hidden state merges onto each layout.
    m_layoutManager->seedDefaultLayoutSettingsIfFresh(ConfigDefaults::defaultLayoutVisibilitySettings());

    // Load layouts (defaultLayout() reads settings internally)
    m_layoutManager->loadLayouts();
    m_layoutManager->loadAssignments();

    // Recalculate zone geometries for ALL layouts so that fixed-mode zones
    // have correct normalized coordinates for preview rendering (KCM, OSD, selector).
    if (QScreen* primary = Utils::primaryScreen()) {
        for (PhosphorZones::Layout* layout : m_layoutManager->layouts()) {
            PhosphorZones::LayoutComputeService::recalculateSync(
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
                        QString screenId = PhosphorScreens::ScreenIdentity::identifierFor(primary);
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
                const PhosphorScreens::PhysicalScreen screen = m_screenManager->screenByName(screenId);
                if (screen.isValid() && screen.qscreen) {
                    m_layoutComputeService->requestRecalculate(
                        layout, screenId,
                        GeometryUtils::effectiveScreenGeometry(m_screenManager.get(), layout, screen.qscreen));
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
    m_previewNotifyTimer.setSingleShot(true);
    m_previewNotifyTimer.setInterval(100);
    connect(&m_previewNotifyTimer, &QTimer::timeout, this, [this]() {
        if (m_algorithmRegistry && m_algorithmRegistry->previewParams() != m_preRetilePreviewParams
            && m_layoutAdaptor) {
            m_layoutAdaptor->notifyLayoutListChanged();
        }
    });

    connect(m_settings.get(), &Settings::settingsChanged, this, [this]() {
        m_overlayService->updateSettings(m_settings.get());

        // Detect state transitions before syncing
        const bool snappingNow = m_settings->snappingEnabled();
        const bool autotileNow = m_settings->autotileEnabled();
        const bool snappingToggled = snappingNow != m_prevSnappingEnabled;
        const bool autotileToggled = autotileNow != m_prevAutotileEnabled;
        m_prevSnappingEnabled = snappingNow;
        m_prevAutotileEnabled = autotileNow;

        // Sync config immediately so the engine never reads stale values.
        // Only retile + preview notification are debounced (100ms timer).
        m_preRetilePreviewParams =
            m_algorithmRegistry ? m_algorithmRegistry->previewParams() : PhosphorTiles::AlgorithmPreviewParams{};
        if (m_autotileEngine) {
            m_autotileEngine->refreshConfigFromSettings();
        }
        m_previewNotifyTimer.start();

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
        if (autotileToggled && !autotileNow && m_windowTrackingAdaptor && m_snapAdaptor && m_snapEngine) {
            // Pre-arm OSD suppression for the resnap signal(s) about to fire (the
            // feedback returns asynchronously, so arm before emitting).
            m_suppressResnapOsd = 1; // resnapCurrentAssignments()
            m_snapAdaptor->resnapCurrentAssignments();
            // Batched float-restore: one resnap signal per autotile-disabled
            // toggle instead of per-window D-Bus chatter. Downcast mirrors
            // signals.cpp's resnap-batching path; a non-snap concrete engine
            // would simply skip the batch (no behaviour regression vs the
            // pre-batch shape, which used per-window D-Bus calls).
            if (auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get())) {
                // updateAutotileScreens() above fired windowsReleased synchronously,
                // populating m_pendingSnapFloatRestores with the snap-float and
                // branch-b snap-zone restores for windows that were floated in
                // autotile. Those windows must be EXCLUDED from the pre-tile geometry
                // restore (they get a float/zone restore instead) and their entries
                // appended to this batch — mirroring the mode-toggle path. Dropping
                // them (the previous behaviour) lost the snap-float restore entirely
                // and left stale entries to corrupt the next toggle's preClaimedZoneIds.
                QSet<QString> restoredWindows;
                for (const ZoneAssignmentEntry& e : m_pendingSnapFloatRestores) {
                    restoredWindows.insert(e.windowId);
                }
                QVector<ZoneAssignmentEntry> entries = buildAutotileRestoreEntries(restoredWindows);
                entries.append(m_pendingSnapFloatRestores);
                m_pendingSnapFloatRestores.clear();
                if (!entries.isEmpty()) {
                    ++m_suppressResnapOsd; // the batched emit drives a second resnap feedback
                    concreteSnap->emitBatchedResnap(entries);
                }
            }
        }

        // Re-resolve the active layout from assignments for the current context.
        // Resnap/retile/OSD is triggered separately by applyAssignmentChanges()
        // after the KCM's batch save completes — NOT here in the settings handler.
        syncModeFromAssignments();
    });

    // Resnap currently-snapped windows when a snapping gap/padding setting
    // changes (global or per-screen) so the new spacing is visible immediately
    // instead of requiring a manual re-snap of each window (discussion #661).
    // The signals below are re-emitted by Settings::load() only when the value
    // actually changed, so this never fires on unrelated saves (colours,
    // shortcuts). Autotile windows are already retiled by the settingsChanged
    // handler above; this covers manually-snapped windows. Debounced so a batch
    // of per-side gap edits in one save collapses into a single resnap pass.
    m_gapResnapTimer.setSingleShot(true);
    m_gapResnapTimer.setInterval(100);
    connect(&m_gapResnapTimer, &QTimer::timeout, this, [this]() {
        if (!m_snapAdaptor) {
            return;
        }
        m_suppressResnapOsd = 1; // settings-driven reflow, not user navigation
        m_snapAdaptor->resnapCurrentAssignments();
    });
    const auto scheduleGapResnap = [this]() {
        m_gapResnapTimer.start();
    };
    connect(m_settings.get(), &Settings::zonePaddingChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::usePerSideOuterGapChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapTopChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapBottomChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapLeftChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::outerGapRightChanged, this, scheduleGapResnap);
    connect(m_settings.get(), &Settings::perScreenSnappingSettingsChanged, this, scheduleGapResnap);

    // Initialize domain-specific D-Bus adaptors
    // Each adaptor has its own D-Bus interface
    // D-Bus adaptors use raw new; Qt parent-child manages their lifetime.
    m_layoutAdaptor =
        new LayoutAdaptor(m_layoutManager.get(), m_virtualDesktopManager.get(), m_screenManager.get(), this);
    m_layoutAdaptor->setActivityManager(m_activityManager.get());
    m_layoutAdaptor->setSettings(m_settings.get());
    m_layoutAdaptor->setAlgorithmRegistry(m_algorithmRegistry.get());
    m_layoutAdaptor->setLayoutSource(m_layoutSources.composite());
    // Thread the bundle-owned autotile source through the adaptor's
    // buildUnifiedLayoutList path so its preview cache survives across
    // D-Bus calls. The full composite above drives the
    // getLayoutPreview* methods; this separate pointer targets only the
    // autotile enumeration slot — see LayoutAdaptor::setAutotileLayoutSource.
    m_layoutAdaptor->setAutotileLayoutSource(m_autotileLayoutSource);
    // Invalidate D-Bus getActiveLayout() cache when the default layout changes in settings
    connect(m_settings.get(), &Settings::defaultLayoutIdChanged, m_layoutAdaptor, &LayoutAdaptor::invalidateCache);
    m_settingsAdaptor = new SettingsAdaptor(m_settings.get(), m_shaderRegistry.get(), &m_profileRegistry, this);

    // Shader adaptor - shader discovery, compilation lifecycle, file monitoring.
    // Held as a member so stop() can detach() it before the unique_ptr member
    // that owns m_shaderRegistry runs its destructor.
    m_shaderAdaptor = new ShaderAdaptor(m_shaderRegistry.get(), this);

    // Window rule adaptor - the unified org.plasmazones.WindowRules surface.
    // Held as a member so stop() can detach() it before the unique_ptr that
    // owns m_windowRuleStore runs its destructor.
    m_windowRuleAdaptor = new WindowRuleAdaptor(m_windowRuleStore.get(), this);

    // Compositor bridge adaptor - compositor-agnostic window control protocol.
    // Held as a member so the support report and the registration watchdog
    // can query its state. Ownership stays with `this` via QObject parent.
    m_compositorBridge = new CompositorBridgeAdaptor(this);

    // Overlay adaptor - overlay visibility and highlighting
    m_overlayAdaptor = new OverlayAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                          m_screenManager.get(), m_settings.get(), this);

    // PhosphorZones::Zone detection adaptor - zone detection queries
    m_zoneDetectionAdaptor = new ZoneDetectionAdaptor(m_zoneDetector.get(), m_layoutManager.get(),
                                                      m_screenManager.get(), m_settings.get(), this);

    // Window tracking adaptor - window-zone assignments
    m_windowTrackingAdaptor =
        new WindowTrackingAdaptor(m_layoutManager.get(), m_zoneDetector.get(), m_screenManager.get(), m_settings.get(),
                                  m_virtualDesktopManager.get(), m_activityManager.get(), this);
    m_windowTrackingAdaptor->setZoneDetectionAdaptor(m_zoneDetectionAdaptor);
    m_windowTrackingAdaptor->setWindowRegistry(m_windowRegistry.get());
    // Full rule set for per-window RestorePosition evaluation (overrides the
    // per-engine *RestoreFloatedWindowsOnLogin settings for matched windows).
    m_windowTrackingAdaptor->setWindowRuleStore(m_windowRuleStore.get());

    // Drop closed windows from m_lastAutotileOrders so a manual→autotile toggle
    // doesn't replay a ghost id into the TilingState (recalculateLayout would
    // then tile N+1 windows for N actual windows).
    connect(m_windowRegistry.get(), &PhosphorEngine::WindowRegistry::windowDisappeared, this,
            [this](const QString& instanceId) {
                pruneAutotileOrdersForWindow(instanceId);
            });

    // Reapply window geometries after each geometry batch (processPendingGeometryUpdates).
    // When the delayed panel requery completes it emits availableGeometryChanged, which triggers
    // the same debounce → processPendingGeometryUpdates → reapply path; no separate delay needed.
    m_reapplyGeometriesTimer.setSingleShot(true);
    connect(&m_reapplyGeometriesTimer, &QTimer::timeout, m_windowTrackingAdaptor,
            &WindowTrackingAdaptor::requestReapplyWindowGeometries);

    // DBusScreenAdaptor::setVirtualScreenConfig writes to Settings (the source
    // of truth) via the IConfigStore — the daemon's single SettingsConfigStore
    // instance, shared with m_screenManager (as its Config::configStore) and
    // m_virtualScreenSwapper. One store per process, one change-signal
    // channel, no parallel Settings observer.
    m_screenAdaptor = new PhosphorScreens::DBusScreenAdaptor(m_screenManager.get(), m_virtualScreenStore.get(), this);

    // Window drag adaptor - handles drag events from KWin script
    // All drag logic (modifiers, zones, snapping) handled here
    m_windowDragAdaptor = new WindowDragAdaptor(m_overlayService.get(), m_zoneDetector.get(), m_layoutManager.get(),
                                                m_screenManager.get(), m_settings.get(), m_windowTrackingAdaptor, this);

    // PhosphorZones::Zone selector methods are called directly from WindowDragAdaptor; QDBusAbstractAdaptor
    // signals are for D-Bus, not Qt connections.

    // Give the window drag adaptor access to the shortcut manager for
    // registering/unregistering the Escape cancel shortcut during drags.
    // Routed through the PhosphorShortcutsIntegration::IAdhocRegistrar interface so the underlying
    // Registry stays private to ShortcutManager.
    m_windowDragAdaptor->setShortcutRegistrar(m_shortcutManager.get());

    // When the compositor bridge re-registers (e.g. KWin reloaded the effect,
    // effect process restarted, or daemon itself restarted mid-drag), any drag
    // state the daemon is still holding is stale — the new effect instance has
    // no knowledge of the prior drag. Clear it eagerly so the next dragStarted
    // from the fresh effect lands on a clean slate instead of silently
    // colliding with a mismatched windowId in the next handler.
    connect(m_compositorBridge, &CompositorBridgeAdaptor::bridgeRegistered, m_windowDragAdaptor,
            [this](const QString& compositorName, const QString&, const QStringList&) {
                qCInfo(lcDaemon) << "Compositor bridge registered (" << compositorName
                                 << ") — clearing any stale drag state held by daemon";
                m_windowDragAdaptor->clearForCompositorReconnect();
            });

    // Registration watchdog: the KWin effect should register as a compositor
    // bridge within a few seconds of startup. If it never does, the daemon is
    // alive but has no window control — drags and shortcuts silently do
    // nothing. Stop the watchdog as soon as the bridge registers; otherwise
    // warnCompositorBridgeMissing() fires once on timeout. Connecting to the
    // adaptor (not m_windowDragAdaptor) so a re-registration also cancels it.
    m_bridgeWatchdogTimer.setSingleShot(true);
    connect(&m_bridgeWatchdogTimer, &QTimer::timeout, this, &Daemon::warnCompositorBridgeMissing);
    connect(m_compositorBridge, &CompositorBridgeAdaptor::bridgeRegistered, &m_bridgeWatchdogTimer,
            [this](const QString&, const QString&, const QStringList&) {
                m_bridgeWatchdogTimer.stop();
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
                                 m_virtualDesktopManager.get(), m_windowRegistry.get());
    auto* autotileEngine = engines.autotile.get();
    auto* snapEngine = engines.snap.get();
    // Move the shared cross-surface resolver BEFORE the engines so it is
    // destroyed AFTER them (they borrow it). Declared earlier than the engines
    // in daemon.h for the same reason.
    m_crossSurfaceResolver = std::move(engines.crossSurfaceResolver);
    m_autotileEngine = std::move(engines.autotile);
    m_snapEngine = std::move(engines.snap);
    m_screenModeRouter = std::move(engines.router);

    // Per-context (window-rule) gap overrides for autotile. Snapping resolves
    // these as the highest-priority gap layer (GeometryUtils::getEffective*);
    // without this, a context gap rule was silently ignored on tiled windows.
    // The closure resolves the screen's CURRENT context here (the engine library
    // stays settings-agnostic) and adapts ContextGapOverride into the
    // PerScreenKeys-shaped map the resolver already consumes.
    if (m_autotileEngine) {
        m_autotileEngine->setContextGapProvider([this](const QString& screenId) -> QVariantMap {
            if (!m_layoutManager) {
                return {};
            }
            const PhosphorZones::ContextGapOverride gaps =
                m_layoutManager->resolveContextGaps(screenId, currentDesktopForScreen(screenId), currentActivity());
            if (gaps.isEmpty()) {
                return {};
            }
            QVariantMap map;
            // zonePadding is snapping's inner spacing — the autotile inner gap.
            if (gaps.zonePadding) {
                map.insert(QStringLiteral("InnerGap"), *gaps.zonePadding);
            }
            if (gaps.outerGap) {
                map.insert(QStringLiteral("OuterGap"), *gaps.outerGap);
            }
            if (gaps.outerGapTop) {
                map.insert(QStringLiteral("OuterGapTop"), *gaps.outerGapTop);
            }
            if (gaps.outerGapBottom) {
                map.insert(QStringLiteral("OuterGapBottom"), *gaps.outerGapBottom);
            }
            if (gaps.outerGapLeft) {
                map.insert(QStringLiteral("OuterGapLeft"), *gaps.outerGapLeft);
            }
            if (gaps.outerGapRight) {
                map.insert(QStringLiteral("OuterGapRight"), *gaps.outerGapRight);
            }
            return map;
        });
    }

    // Build the PhosphorContext::ContextResolver wiring NOW — after the
    // workspace managers, settings, and router exist; before any D-Bus
    // adaptor or OverlayService method that consumes it runs. Three
    // narrow adapters one-line forward to the existing services; the
    // resolver borrows them. Declaration order in daemon.h guarantees
    // reverse-tear-down: resolver first, then adapters, then services.
    m_workspaceStateAdapter =
        std::make_unique<DaemonWorkspaceStateAdapter>(m_virtualDesktopManager.get(), m_activityManager.get());
    m_screenModeAdapter = std::make_unique<DaemonScreenModeAdapter>(m_screenModeRouter.get());
    m_settingsGateAdapter = std::make_unique<DaemonSettingsGateAdapter>(m_settings.get(), m_layoutManager.get());
    m_contextResolver = std::make_unique<PhosphorContext::ContextResolver>(
        m_workspaceStateAdapter.get(), m_screenModeAdapter.get(), m_settingsGateAdapter.get());

    // Late-bind the resolver into the D-Bus adaptors that gate their
    // handlers on the disable/lock cascade. Each adaptor was constructed
    // earlier (before m_settings/m_screenModeRouter were ready); the
    // resolver only exists now. The setters mirror setAutotileEngine /
    // setShortcutRegistrar / setScreenModeRouter — same late-binding
    // pattern the daemon already uses for cross-cutting deps.
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setContextResolver(m_contextResolver.get());
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setContextResolver(m_contextResolver.get());
    }
    // m_snapAdaptor is constructed below at the engine-adaptor block; its
    // contextResolver wire lives there.

    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::settingsPersistRequested, this, [this]() {
        if (m_settings) {
            m_settings->save();
        }
    });

    autotileEngine->refreshConfigFromSettings();

    // Give the window drag adaptor access to the autotile engine for per-screen
    // autotile checks (overlay suppression and snap rejection on autotile screens).
    // Uses the base-class pointer — WDA only needs isActiveOnScreen().
    m_windowDragAdaptor->setAutotileEngine(m_autotileEngine.get());

    // SnapEngine creates its own SnapState internally (symmetric with
    // AutotileEngine/TilingState). WTS references it for zone queries.
    m_windowTrackingAdaptor->service()->setSnapState(snapEngine->snapState());
    m_windowTrackingAdaptor->service()->setSnapEngine(snapEngine);
    // Inject the shared window registry so SnapState canonicalizes its
    // windowId-keyed stores to the stable first-seen composite (instanceId →
    // first observed appId|instanceId). This makes snap float/zone/screen state
    // immune to the effect-restart-after-WM_CLASS-mutation re-identification
    // skew, mirroring how AutotileEngine canonicalizes tiling state (issue #628).
    snapEngine->snapState()->setWindowRegistry(m_windowRegistry.get());

    // Filter the unified rule store down to its Exclude-shaped slice and
    // hand the address to SnapEngine for its isAppIdExcluded probe. The
    // filtered slice is held as a stable Daemon member (m_excludeRuleSet)
    // and refreshed in-place via setRules so the bound RuleEvaluator's
    // per-revision sort index and resolve cache actually invalidate on
    // each rules-changed edit (a copy-assigned fresh WindowRuleSet would
    // re-import revision=1 every cycle, freezing the cache on the next
    // resolveCached-bearing migration of the call sites). Rebuilt
    // whenever the unified store emits rulesChanged, so a settings-app
    // rule edit propagates without a manual refresh.
    //
    // Initial wiring happens once below, outside the rulesChanged lambda:
    //   - `setExcludeRuleSet(&m_excludeRuleSet)` hands SnapEngine the
    //     stable address. The pointer never changes after this; the
    //     evaluator picks up subsequent in-place edits through the
    //     revision counter, so subsequent re-fences would be no-ops.
    //   - The first `setRules` + `pruneExcludedPendingRestores` priming
    //     pair seeds the filter and drains any restore queue entries
    //     populated by WTA::loadState above.
    snapEngine->setExcludeRuleSet(&m_excludeRuleSet);
    m_excludeRuleSet.setRules(
        PhosphorWindowRules::ExclusionRules::excludeRulesFrom(m_windowRuleStore->ruleSet()).rules());
    m_windowTrackingAdaptor->pruneExcludedPendingRestores(
        PhosphorWindowRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));

    auto refilterExcludeRules = [this, snapEnginePtr = QPointer(snapEngine)] {
        // QPointer null-checks defend the rulesChanged subscription
        // against the shutdown window where m_snapEngine.reset() has
        // already fired but the subscription has not yet auto-
        // disconnected via ~Daemon (the connection's `this`-context only
        // breaks on Daemon destruction, not on a member reset). Mirrors
        // the QPointer pattern used by the persistence-delegate and
        // signal-relay lambdas below.
        if (!snapEnginePtr) {
            return;
        }
        // Symmetric guard for the rule store. `m_windowRuleStore` is a
        // unique_ptr owned by Daemon, so it currently shares Daemon's
        // lifetime; the guard exists so a future refactor that drops
        // and re-creates the store on the fly (or moves ownership
        // out) can't UAF this lambda.
        if (!m_windowRuleStore) {
            return;
        }
        // Equality-guard against no-op edits: every rulesChanged emission
        // (rename, priority change, non-Exclude action edit, …) fires
        // this lambda, but only changes that affect the Exclude slice
        // should bump the evaluator's revision and walk the (potentially
        // long) pending-restore queues. The guard below compares the two
        // `QList<WindowRule>` slices element-wise (the same semantics as
        // `WindowRuleSet::operator==`, which delegates to this list compare) —
        // exactly the rules-list-only comparison we want.
        const QList<PhosphorWindowRules::WindowRule> newSlice =
            PhosphorWindowRules::ExclusionRules::excludeRulesFrom(m_windowRuleStore->ruleSet()).rules();
        if (newSlice == m_excludeRuleSet.rules()) {
            return;
        }
        // Cache invalidation for matched windows happens through the
        // `setRules` revision bump; the evaluator inside SnapEngine
        // reads `m_excludeRuleSet`'s revision and drops its per-revision
        // index / cache automatically. No `setExcludeRuleSet` re-fence
        // — the pointer was wired once at init above.
        m_excludeRuleSet.setRules(newSlice);
        // Prune any pending-restore queues for apps now covered by an
        // Exclude rule. Snap-engine's resolveWindowRestore already refuses
        // them at runtime, but stale queue entries spam logs and bloat the
        // saved state. The autotile-side queues don't exist yet at init
        // — daemon/signals.cpp's finalizeStartup re-runs the prune once
        // AutotileEngine::loadState has populated them.
        if (m_windowTrackingAdaptor) {
            // Shutdown-window guard, mirrors snapEnginePtr null-check above.
            m_windowTrackingAdaptor->pruneExcludedPendingRestores(
                PhosphorWindowRules::ExclusionRules::applicationExcludePatternsFrom(m_excludeRuleSet));
        }
    };
    connect(m_windowRuleStore.get(), &PhosphorWindowRules::WindowRuleStore::rulesChanged, this,
            [refilterExcludeRules](bool /*persisted*/) {
                refilterExcludeRules();
            });

    // A rule edit can change the live context-lock state (e.g. toggling,
    // re-prioritising or re-matching a LockContext rule) without touching the
    // manual lock store, so the ISettings::settingsChanged refresh that keeps
    // open zone selectors / the layout picker in sync would miss it. Re-push
    // the lock state to any open overlay on every rule change. QPointer guards
    // the shutdown window (overlay reset before ~Daemon disconnects).
    connect(m_windowRuleStore.get(), &PhosphorWindowRules::WindowRuleStore::rulesChanged, this,
            [overlay = QPointer(m_overlayService.get())](bool /*persisted*/) {
                if (overlay) {
                    overlay->refreshContextLockState();
                    // A rule change can also alter the resolved overlay shader /
                    // style for the active context; re-apply it live if the
                    // overlay is currently shown (no-op otherwise).
                    overlay->refreshOverlayPropertiesIfShown();
                }
            });

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

    // ───────────────────────────────────────────────────────────────────────────
    // Per-engine float state (root fix for the shared-bit float defect).
    //
    // Float state is genuinely per-engine: a window floated in autotile mode is
    // NOT floating in snapping mode and vice versa. The authoritative store lives
    // in each engine (SnapEngine→SnapState::isFloating / AutotileEngine→
    // TilingState::isFloating). WTS is engine-agnostic (LGPL boundary), so we
    // inject a resolver (reader) and writer that route to the engine owning the
    // window's CURRENT screen mode. This replaces the old single shared
    // m_floatingWindows + m_snapState bit that both engines read/wrote.
    //
    // Mode resolution: the window's tracked screen (WTS screenAssignments, with
    // the autotile engine's own tracked screen as the fallback for windows snap
    // never saw) → LayoutRegistry::modeForScreen → the owning engine.
    {
        auto screenModeForWindow = [this, autotilePtr = QPointer(autotileEngine)](
                                       const QString& windowId) -> PhosphorZones::AssignmentEntry::Mode {
            QString screenId;
            if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
                screenId = m_windowTrackingAdaptor->service()->screenForWindow(windowId);
            }
            if (!screenId.isEmpty() && m_layoutManager) {
                return m_layoutManager->modeForScreen(screenId, currentDesktopForScreen(screenId), currentActivity());
            }
            // No tracked screen in WTS (e.g. a window snap never saw): if the
            // autotile engine tracks it, its current mode is Autotile. Otherwise
            // default to Snapping — the historical no-context fallback.
            if (autotilePtr && autotilePtr->isWindowTracked(windowId)) {
                return PhosphorZones::AssignmentEntry::Autotile;
            }
            return PhosphorZones::AssignmentEntry::Snapping;
        };

        m_windowTrackingAdaptor->service()->setEngineFloatResolver(
            [screenModeForWindow, snapEnginePtr = QPointer(snapEngine),
             autotilePtr = QPointer(autotileEngine)](const QString& windowId) -> bool {
                if (screenModeForWindow(windowId) == PhosphorZones::AssignmentEntry::Autotile) {
                    return autotilePtr && autotilePtr->isWindowFloatingInAutotile(windowId);
                }
                return snapEnginePtr && snapEnginePtr->snapState() && snapEnginePtr->snapState()->isFloating(windowId);
            });

        m_windowTrackingAdaptor->service()->setEngineFloatWriter(
            [screenModeForWindow, snapEnginePtr = QPointer(snapEngine)](const QString& windowId, bool floating) {
                // Write ONLY the snap engine's authoritative float store, and
                // only for snap-mode windows. The two engines keep INDEPENDENT
                // float state — writing the snap bit for an autotile-mode window
                // is exactly the cross-mode leak this refactor eliminates.
                //
                // Autotile-mode windows are intentionally a no-op here:
                // TilingState::isFloating is the autotile engine's authoritative
                // float store and is already set by the engine itself (via
                // performToggleFloat / setWindowFloat) BEFORE any daemon sync
                // calls WTS::setWindowFloating. Re-driving setWindowFloat here
                // would re-toggle the float and retile — so the engine stays the
                // sole owner of its own float bit.
                if (screenModeForWindow(windowId) == PhosphorZones::AssignmentEntry::Autotile) {
                    return;
                }
                if (snapEnginePtr && snapEnginePtr->snapState()) {
                    snapEnginePtr->snapState()->setFloating(windowId, floating);
                }
            });

        m_windowTrackingAdaptor->service()->setEngineFloatLister(
            [snapEnginePtr = QPointer(snapEngine), autotilePtr = QPointer(autotileEngine)]() -> QStringList {
                QStringList all;
                if (snapEnginePtr && snapEnginePtr->snapState()) {
                    all += snapEnginePtr->snapState()->floatingWindows();
                }
                if (autotilePtr) {
                    all += autotilePtr->allFloatingWindows();
                }
                return all;
            });

        // Owning-engine predicate: WTS answers isWindowInAutotileMode with this
        // (the single owning-engine signal for the capture funnel + float
        // routing), using the same screen→mode resolution as the float resolver
        // above. Float-back geometry itself is single-sourced from the unified
        // WindowPlacementStore, so no per-engine geometry wiring is needed.
        m_windowTrackingAdaptor->service()->setAutotileModePredicate(
            [screenModeForWindow](const QString& windowId) -> bool {
                return screenModeForWindow(windowId) == PhosphorZones::AssignmentEntry::Autotile;
            });

        // Tiled predicate (distinct from the MODE predicate above): live
        // engine state, "is this window actively tiled right now". Guards
        // recordFreeGeometry against recording a tile rect as a float-back —
        // the engine-backed answer survives effect reloads, which the
        // effect-side capture guard cannot.
        m_windowTrackingAdaptor->service()->setAutotileTiledPredicate(
            [autotilePtr = QPointer(autotileEngine)](const QString& windowId) -> bool {
                return autotilePtr && autotilePtr->isWindowTiled(windowId);
            });
    }

    // Wire SnapEngine's back-reference to the window tracking adaptor.
    // SnapEngine's navigation methods (focusInDirection, moveFocusedInDirection, …)
    // were moved out of WindowTrackingAdaptor and need to reach back into the
    // adaptor for shared state that hasn't been migrated yet: the target
    // resolver, the last-active window/screen shadow, and the snap-
    // bookkeeping helpers (windowSnapped, windowUnsnapped, recordSnapIntent,
    // clearPreTileGeometry). A future refactor should move that state onto
    // SnapEngine or PhosphorPlacement::WindowTrackingService and retire the back-reference.
    snapEngine->setNavigationStateProvider(m_windowTrackingAdaptor);

    // Clear stale autotile-floated flag when a window is snapped. A window
    // dragged from an autotile VS to a snap VS retains its autotileFloated
    // marker; without this, a subsequent mode change on the autotile VS
    // incorrectly processes the already-snapped window as autotile-managed.
    // Wired here (daemon) because engines must not know about each other.
    connect(snapEngine, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
            [this](const QString& windowId, const PhosphorProtocol::WindowStateEntry&) {
                if (m_autotileEngine) {
                    m_autotileEngine->clearModeSpecificFloatMarker(windowId);
                }
            });

    // ScreenModeRouter was created by createEngines() above; wire it to WTA.
    m_windowTrackingAdaptor->setScreenModeRouter(m_screenModeRouter.get());

    // m_virtualScreenStore is constructed in the initializer list (it's a
    // Config arg for m_screenManager). The swapper is constructed here
    // because navigation handlers don't run before init() returns anyway.
    m_virtualScreenSwapper = std::make_unique<PhosphorScreens::VirtualScreenSwapper>(m_virtualScreenStore.get());

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
    // Autotile restore persistence (window orders + pending restores) is now
    // subsumed by the unified WindowPlacementStore — an autotiled window's position
    // is one WindowPlacement record, captured by the common save-time snapshot and
    // close hook and restored on reopen by AutotileEngine::insertWindow. Like snap,
    // there is no engine-specific serialize delegate.

    // Trigger a placement save when the autotile layout changes (window added /
    // removed / reordered / floated). markDirty(DirtyWindowPlacements) emits
    // stateChanged → scheduleSaveState (wired in the adaptor ctor), and saveState's
    // refreshOpenWindowPlacements re-captures every open window's current placement
    // (including autotiled positions) into the unified store before writing. This
    // placementChanged bridge is autotile-specific: snap captures directly on
    // windowSnapStateChanged → captureWindowPlacement, whereas autotile has no
    // per-window signal, so its per-screen placementChanged schedules the save and
    // the save-time snapshot does the per-window capture.
    connect(autotileEngine, &PhosphorEngine::PlacementEngineBase::placementChanged, m_windowTrackingAdaptor, [this]() {
        if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
            m_windowTrackingAdaptor->service()->markDirty(
                PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
        }
    });

    // Create engine D-Bus adaptors — each engine has a dedicated adaptor that
    // connects signals in its constructor (unified pattern for both engines)
    m_snapAdaptor = new SnapAdaptor(snapEngine, m_windowTrackingAdaptor, m_settings.get(), this);
    m_snapAdaptor->setContextResolver(m_contextResolver.get());
    m_autotileAdaptor = new AutotileAdaptor(autotileEngine, m_screenManager.get(), m_algorithmRegistry.get(), this);

    // Control adaptor - high-level convenience API for third-party integrations.
    // Held as a member so stop() can detach() it before the unique_ptr members
    // it borrows are destroyed.
    m_controlAdaptor =
        new ControlAdaptor(m_windowTrackingAdaptor, m_snapAdaptor, m_layoutAdaptor, m_layoutManager.get(),
                           autotileEngine, m_screenManager.get(), m_compositorBridge, this);

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
                // Per-output virtual desktops (#648): each screen resolves its own desktop.
                const int desktop = currentDesktopForScreen(screenId);
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
            // Restore snap-float positions for windows this KCM apply released
            // from autotile — the buffer-based resnap above cannot cover
            // floating windows (see the helper).
            emitPendingSnapFloatRestoresForResnapBuffer();

            // Show OSD for changed screens — use locked OSD variant when context is locked.
            // KCM Apply is an explicit user-driven layout assignment change, so the regular
            // preview OSDs gate on showOsdOnLayoutSwitch (matching cycle / quick-layout /
            // zone-selector-drop). The locked-context OSD bypasses the toggle by design — it
            // explains why a requested change had no visible effect on that screen, the same
            // pattern used for the mode-toggle locked feedback in connectShortcutSignals().
            const bool osdEnabled = m_settings && m_settings->showOsdOnLayoutSwitch();
            for (const auto& osd : std::as_const(osdEntries)) {
                // Suppressed context → no active layout; skip its OSD, mirroring
                // the per-screen desktop-switch OSD gate in showOsdForScreens.
                if (m_layoutManager
                    && m_layoutManager->isContextActiveLayoutSuppressed(
                        osd.screenId, currentDesktopForScreen(osd.screenId), activity)) {
                    continue;
                }
                const PhosphorZones::AssignmentEntry::Mode mode = osd.isAutotile
                    ? PhosphorZones::AssignmentEntry::Autotile
                    : PhosphorZones::AssignmentEntry::Snapping;
                if (isCurrentContextLockedForMode(osd.screenId, mode)) {
                    showLockedPreviewOsd(osd.screenId);
                } else if (!osdEnabled) {
                    continue;
                } else if (osd.isAutotile) {
                    if (!osd.algoId.isEmpty()) {
                        // Resolve the algorithm's human-readable display
                        // name via the registry instead of surfacing the
                        // wire-format id (e.g. "bsp" → "Binary Split").
                        // Mirrors the algorithm display-name resolution in the
                        // per-screen OSD path (showOsdForScreens, osd.cpp).
                        const auto* algo = m_algorithmRegistry ? m_algorithmRegistry->algorithm(osd.algoId) : nullptr;
                        const QString displayName = algo ? algo->name() : osd.algoId;
                        showLayoutOsdForAlgorithm(osd.algoId, displayName, osd.screenId);
                    }
                } else {
                    // Per-output virtual desktops (#648): each screen resolves its own desktop.
                    const int desktop = currentDesktopForScreen(osd.screenId);
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
    // Worst-case blocking: 100 + 200 + 400 = 700 ms on the GUI thread.
    // init() runs before QGuiApplication::exec(), so QTimer-based async
    // approaches don't fire — synchronous sleep is the only retry path
    // available here. The retry is bounded by `maxRetries`, and a bus
    // disconnect during the wait would render every subsequent retry
    // pointless (lastError type stays ServiceUnknown but the actual
    // problem is connection-level).
    bool serviceRegistered = false;
    for (int attempt = 0; attempt < maxRetries; ++attempt) {
        if (!bus.isConnected()) {
            qCCritical(lcDaemon) << "D-Bus bus connection lost mid-retry — aborting service registration";
            return false;
        }
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
            [this](const QString& zoneId, const PhosphorProtocol::ZoneGeometryRect& geometry) {
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

    // Reset the shutdown latch — stop() sets it true and nothing else clears
    // it, so a stop()→start() cycle (tests, programmatic restart) would
    // permanently silence every shutdown-guarded code path
    // (warnCompositorBridgeMissing, late-arrival reply guards, OSD
    // suppression in daemon/osd.cpp) on the second run. m_aboutToQuitConnected
    // already contemplates this cycle to avoid stacking the aboutToQuit
    // handler; this is the matching reset on the value side.
    m_shuttingDown = false;

    // Re-publish the QML static defaults. stop() nulls all three
    // (`PhosphorCurve::setDefaultRegistry(nullptr)` etc.) to prevent
    // borrowed-pointer UAF during teardown; without this re-publish,
    // a stop()→start() cycle (tests, programmatic restart) leaves QML
    // resolving against nullptr defaults — every
    // `PhosphorMotionAnimation { profile: … }` and every clock-driven
    // animated-value lookup silently fails until the next ctor runs.
    // The setters are idempotent: storing the same pointer the ctor
    // installed is a no-op on the first start() of a fresh daemon.
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(&m_curveRegistry);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&m_profileRegistry);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(m_clockManager.get());

    // Suppress OSDs once Qt begins shutdown (SIGTERM, programmatic quit).
    // Connected once — m_aboutToQuitConnected prevents stacking on stop()→start().
    if (qGuiApp && !m_aboutToQuitConnected) {
        connect(qGuiApp, &QGuiApplication::aboutToQuit, this, [this]() {
            m_shuttingDown = true;
        });
        m_aboutToQuitConnected = true;
    }

    // Detect phantom plasma-restore sessions via systemd's user bus.
    // See queryPlasmaWorkspaceState() for the full rationale.
    queryPlasmaWorkspaceState();

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
    // PhosphorWorkspaces::VirtualDesktopManager and PhosphorWorkspaces::ActivityManager no longer resolve layouts —
    // this is the single code path that understands autotile vs snapping mode.
    syncModeFromAssignments();

    finalizeStartup();

    // Migrate window screen assignments from physical to virtual IDs.
    // Must run AFTER finalizeStartup() which loads WTA state — otherwise
    // the migration finds no windows to migrate.
    migrateStartupScreenAssignments();

    // Intentionally last: the algorithmChanged handler (signals.cpp) and showDesktopSwitchOsd
    // (osd.cpp) both gate on !m_running to suppress OSD/feedback during startup. finalizeStartup()
    // calls m_autotileEngine->loadState() which synchronously emits algorithmChanged, and
    // KWin/Plasma can deliver desktop/activity-change signals during the same window. Setting
    // m_running before finalizeStartup() returns would let those handlers fire and double-queue
    // (or leak past) the startup OSD that finalizeStartup() is responsible for.
    m_running = true;
    // NOTE: daemonReady() is emitted by finalizeStartup() — do NOT emit again here.

    // Arm the compositor-bridge registration watchdog. See
    // BRIDGE_WATCHDOG_TIMEOUT_MS for the grace-period rationale. Skip if the
    // effect already registered during init().
    if (m_compositorBridge && !m_compositorBridge->isBridgeRegistered()) {
        m_bridgeWatchdogTimer.start(BRIDGE_WATCHDOG_TIMEOUT_MS);
    }
}

void Daemon::warnCompositorBridgeMissing()
{
    // Stay silent during shutdown. The watchdog may still be armed when the
    // session ends, and a warning/notification raised on the way out is just
    // noise — mirrors the OSD suppression gated on m_running/m_shuttingDown.
    if (m_shuttingDown) {
        return;
    }

    // Re-check: the watchdog is stopped on bridgeRegistered, but a registration
    // landing in the same event-loop turn as the timeout could still reach
    // here. Treat a registered bridge as success and stay silent.
    if (!m_compositorBridge || m_compositorBridge->isBridgeRegistered()) {
        return;
    }

    // Inspect the installed effect plugin (synchronous, cheap). The most common
    // silent failure is a stale effect build whose IID no longer matches the
    // running KWin, so KWin's effect loader rejects it without surfacing an
    // error and the effect never registers.
    bool effectInstalled = false;
    const QString effectKWinVersion = probeEffectKWinVersion(effectInstalled);

    if (!effectInstalled) {
        emitBridgeMissingWarning(
            PhosphorI18n::tr("The PlasmaZones KWin effect plugin is not installed where KWin can find it. "
                             "Reinstall PlasmaZones."));
        return;
    }
    if (effectKWinVersion.isEmpty()) {
        // Plugin present but its IID is not a recognizable KWin effect IID —
        // nothing specific to report, fall back to the generic guidance.
        emitBridgeMissingWarning(QString());
        return;
    }

    // Compare the effect's build-time KWin version against the running KWin.
    // supportInformation() is the only reliable D-Bus source for KWin's
    // version; query it asynchronously so this degraded startup path never
    // blocks the daemon's event loop (mirrors the fire-and-forget notification
    // call in emitBridgeMissingWarning).
    QDBusMessage req =
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                       QStringLiteral("org.kde.KWin"), QStringLiteral("supportInformation"));
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(req, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, effectKWinVersion](QDBusPendingCallWatcher* call) {
                call->deleteLater();

                // The 3s round-trip widens the window in which a late effect
                // registration can land; stay silent if shutdown began or the
                // bridge registered after all.
                if (m_shuttingDown || (m_compositorBridge && m_compositorBridge->isBridgeRegistered())) {
                    return;
                }

                QString diagnosis;
                const QDBusPendingReply<QString> reply = *call;
                if (!reply.isError()) {
                    const QRegularExpressionMatch match =
                        QRegularExpression(QStringLiteral("KWin version:\\s*(\\S+)")).match(reply.value());
                    if (match.hasMatch()) {
                        const QString runningKWinVersion = match.captured(1);
                        if (runningKWinVersion != effectKWinVersion) {
                            diagnosis = PhosphorI18n::tr(
                                            "The PlasmaZones KWin effect was built for KWin %1 but "
                                            "KWin %2 is running, so KWin will not load it. Rebuild and "
                                            "reinstall PlasmaZones against the running KWin. On NixOS, "
                                            "install via the flake's nixosModules or overlay (not "
                                            "packages.default).")
                                            .arg(effectKWinVersion, runningKWinVersion);
                        }
                    }
                }
                emitBridgeMissingWarning(diagnosis);
            });
}

void Daemon::emitBridgeMissingWarning(const QString& diagnosis)
{
    if (diagnosis.isEmpty()) {
        qCWarning(lcDaemon) << "Compositor bridge did not register within" << (BRIDGE_WATCHDOG_TIMEOUT_MS / 1000)
                            << "s of startup — the PlasmaZones KWin effect is not running or"
                            << "failed to register. Window dragging, keyboard shortcuts, and"
                            << "snapping will not work. Enable the PlasmaZones effect in System"
                            << "Settings > Desktop Effects, then restart the Plasma session so"
                            << "KWin loads it.";
    } else {
        qCWarning(lcDaemon) << "Compositor bridge did not register within" << (BRIDGE_WATCHDOG_TIMEOUT_MS / 1000)
                            << "s of startup — window control is dead." << diagnosis;
    }

    const QString body = diagnosis.isEmpty()
        ? PhosphorI18n::tr(
              "The PlasmaZones KWin effect has not registered with the daemon, so window "
              "dragging and shortcuts will not work. Make sure it is enabled in System "
              "Settings > Desktop Effects, then restart the Plasma session.")
        : diagnosis;

    // Raise a desktop notification via the freedesktop spec so the user sees
    // the problem without having to read the journal. A direct method call
    // (rather than QDBusInterface) keeps this off the main thread's critical
    // path: QDBusInterface's constructor does a blocking Introspect round-trip,
    // whereas createMethodCall + asyncCall is genuinely fire-and-forget. A
    // missing notification server just makes the async call error out, which
    // is fine. Mirrors the createMethodCall pattern used elsewhere in daemon.
    QDBusMessage notify = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
        QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
    notify << QStringLiteral("PlasmaZones") // app_name
           << 0u // replaces_id
           << QStringLiteral("plasmazones") // app_icon
           << PhosphorI18n::tr("PlasmaZones: window manager integration inactive") // summary
           << body // body
           << QStringList() // actions
           << QVariantMap() // hints
           << -1; // timeout (server default)
    QDBusConnection::sessionBus().asyncCall(notify);
}

void Daemon::stop()
{
    m_shuttingDown = true;

    // Cancel any pending debounced gap-resnap so it can't fire mid-teardown
    // (the engine is cleared below; a late fire would be a wasted no-op).
    m_gapResnapTimer.stop();

    // Drop the layout-manager provider lambdas FIRST, before the m_running
    // gate. They capture `this` and dereference m_settings; m_settings is
    // declared after m_layoutManager, so reverse-order member destruction
    // tears m_settings down BEFORE m_layoutManager. Any cascade query
    // during ~LayoutRegistry that hits a still-installed lambda would
    // dereference freed memory.
    //
    // The m_running gate skips the rest of stop() on init-without-start
    // paths (test fixtures, early-fail constructors, double-stop). The
    // providers, however, are installed in init() — which runs before
    // m_running is set in start(). Clearing them must therefore not be
    // gated, otherwise the dangling-lambda UAF still reaches the
    // member-destruction window. clearing is null-safe and idempotent
    // (an already-cleared std::function clears to itself).
    if (m_layoutManager) {
        m_layoutManager->setDefaultLayoutIdProvider({});
        m_layoutManager->setDefaultAutotileAlgorithmProvider({});
        m_layoutManager->setSnappingPreferredProvider({});
        m_layoutManager->setDefaultAssignmentSuppressedProvider({});
    }

    // Null the QML static registry / manager pointers BEFORE the m_running
    // gate. These three statics are published unconditionally from
    // `setupAnimationProfiles()` in the ctor — which runs before `init()`
    // or `start()`. A Daemon constructed but never started (test fixtures,
    // early-fail init paths) still has them pinned to the about-to-die
    // members, so the clear must run on every teardown path, not just the
    // post-start one. Same "borrowed-pointer + late member destruction"
    // window as the provider lambdas above. The setDefault*(nullptr)
    // calls are unconditionally null-safe.
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(nullptr);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(nullptr);

    if (!m_running) {
        return;
    }

    // Tear down the daemon-owned PhosphorProfileRegistry entries this
    // Daemon published so a later Daemon reconstruction (tests, or a
    // live reconfigure that tears down and rebuilds the daemon in
    // place) starts from a registry owning none of our entries. Mirrors
    // the narrow-clear policy in `setupAnimationProfiles()` — a
    // wholesale `clear()` would also evict any other consumer's
    // entries if they happened to register before us.
    //
    // The two partitions we publish under:
    //
    //   - Settings-driven direct entries (`Global`, …): registered by
    //     `publishActiveAnimationProfile` under the direct-owner tag.
    //     Unregister each path here.
    //   - Loader-owned user-JSON entries (tagged
    //     `kPlasmaZonesUserProfilesOwnerTag`): we explicitly reset
    //     `m_profileLoader` and `m_curveLoader` here so the loader
    //     destructors run NOW (issuing their own `clearOwner(ownerTag)`
    //     and tearing down the QFileSystemWatchers) rather than at
    //     `~Daemon` body where they'd fire path-change signals into a
    //     half-destroyed object during member destruction order.
    {
        auto& profileRegistry = m_profileRegistry;
        for (const QString* path : kSettingsDrivenProfilePaths) {
            profileRegistry.unregisterProfile(*path);
        }
    }

    // Stop the publish coalescing trampoline before resetting the
    // loaders — the timer is a member QTimer, so its `timeout` slot
    // would otherwise still fire on the next event-loop tick after
    // m_settings (its data source) has been destroyed.
    m_animationPublishTimer.stop();
    m_animationPublishPending = false;

    // Reset the loaders explicitly so the QFileSystemWatcher inside
    // each is torn down NOW, before any other shutdown step has a
    // chance to spin the event loop. Without this, the unique_ptrs
    // would only destruct at the end of the ~Daemon body, leaving a
    // window where stale path-change signals could fire into a
    // half-destroyed object — visible in tests that re-construct the
    // daemon, and theoretically observable in production on a
    // configure-reload cycle. ProfileLoader's destructor issues its
    // own `clearOwner(kPlasmaZonesUserProfilesOwnerTag)` so the
    // process-global PhosphorProfileRegistry shed those entries here.
    m_profileLoader.reset();
    m_curveLoader.reset();
    m_shaderBakePool.clear();
    m_shaderBakePool.waitForDone(500);
    if (m_overlayService) {
        m_overlayService->setAnimationShaderRegistry(nullptr);
    }
    m_animationShaderRegistry.reset();

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

    // Autotile per-window restore state is included in WTA's saveStateOnShutdown()
    // above via the unified WindowPlacementStore (refreshOpenWindowPlacements
    // captures every open window's placement). No separate save needed.
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

    // Clear the late-bound WTS float / mode callbacks that capture `this` (Daemon,
    // via screenModeForWindow) — symmetric with the setShouldTrackPredicate /
    // setShouldRestorePredicate clears, so the "every `this`-capturing predicate is
    // cleared before teardown" contract stays grep-discoverable and survives a
    // future ownership/order refactor.
    if (m_windowTrackingAdaptor && m_windowTrackingAdaptor->service()) {
        auto* wts = m_windowTrackingAdaptor->service();
        wts->setEngineFloatResolver({});
        wts->setEngineFloatWriter({});
        wts->setEngineFloatLister({});
        wts->setAutotileModePredicate({});
        wts->setAutotileTiledPredicate({});
    }

    // Tear down the context-resolver triple before destroying the
    // services the adapters borrow from. Order: borrowers (D-Bus
    // adaptors) drop their non-owning resolver pointer first, then the
    // resolver and its three adapters die, then the underlying router
    // / VirtualDesktopManager / ActivityManager / Settings can safely
    // reset(). Without this, a queued D-Bus method that lands between
    // here and ~Daemon (or a shortcut-manager signal still alive on the
    // main thread) would deref an adapter whose backing service had
    // already been freed by the existing engine-pointer teardown below.
    //
    // Explicit symmetric clear across all three borrowers — SnapAdaptor's
    // resolver is also nulled defensively by clearEngine() above, but doing
    // it here too keeps the teardown contract grep-discoverable and survives
    // a future refactor of clearEngine() that might stop touching the
    // resolver pointer.
    if (m_snapAdaptor) {
        m_snapAdaptor->setContextResolver(nullptr);
    }
    if (m_windowDragAdaptor) {
        m_windowDragAdaptor->setContextResolver(nullptr);
    }
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setContextResolver(nullptr);
        // m_screenModeRouter is destroyed below; null its WTA borrow
        // before that reset so any D-Bus call landing in the gap
        // between this teardown and the bus unregister can't deref
        // a freed router pointer. SnapAdaptor's clearEngine() does
        // the symmetric clear (snapadaptor.cpp).
        m_windowTrackingAdaptor->setScreenModeRouter(nullptr);
    }
    m_contextResolver.reset();
    m_settingsGateAdapter.reset();
    m_screenModeAdapter.reset();
    m_workspaceStateAdapter.reset();

    // Destroy the router. Engines below outlive it so any in-flight
    // navigatorForShortcut path completes with the engine pointers it
    // already captured before the router went away.
    m_screenModeRouter.reset();

    // Sever SnapEngine's borrow of m_excludeRuleSet (a daemon-owned value
    // member) BEFORE m_snapEngine.reset(). Declaration order currently
    // guarantees lifetime, but a future reorder or ownership move could
    // silently introduce a dangling pointer through isAppIdExcluded if
    // a late shutdown call landed; the explicit clear here makes the
    // teardown contract grep-discoverable and survives that refactor.
    // `m_snapEngine` is base-typed `PlacementEngineBase*`; the setter
    // lives on the concrete `SnapEngine`. qobject_cast mirrors the
    // narrowing in the autotile-toggle branch above (~ line 893).
    if (auto* concreteSnap = qobject_cast<PhosphorSnapEngine::SnapEngine*>(m_snapEngine.get())) {
        concreteSnap->setExcludeRuleSet(nullptr);
    }

    // Likewise sever WindowTrackingAdaptor's borrow of m_windowRuleStore (used by
    // its restore-position evaluator) before the store is destroyed. Same
    // grep-discoverable teardown contract as the SnapEngine exclude borrow above.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setWindowRuleStore(nullptr);
    }

    // Destroy engines now (during stop(), before Qt child destruction order).
    m_snapEngine.reset();
    m_autotileEngine.reset();

    // Both engines borrowed m_crossSurfaceResolver (injected at construction).
    // They are destroyed immediately above, so the borrow is already dead;
    // reset the resolver here too so the teardown order is explicit and
    // grep-discoverable — matching the exclude-rule / window-rule borrow
    // severing above — and survives a future member-declaration reorder.
    m_crossSurfaceResolver.reset();

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
    // WHY ONLY THESE FOUR: SettingsAdaptor has the confirmed dtor-UAF
    // (debounced save timer flush). ShaderAdaptor + ControlAdaptor have
    // non-trivial signal wiring + cached state that benefits from
    // explicit teardown for the same "queued D-Bus call lands during
    // destruction window" defense-in-depth. WindowRuleAdaptor borrows
    // m_windowRuleStore (a unique_ptr) and m_settings; without detach
    // its slot bodies could deref freed memory during the window after
    // ~Daemon's body returns — that is when the unique_ptr members
    // (including m_windowRuleStore) run their destructors, and the
    // raw-Qt-parented WindowRuleAdaptor only runs its own destructor
    // *after* that, as part of QObject child cleanup.
    //
    // The other nine raw-Qt-parented adaptors (LayoutAdaptor,
    // OverlayAdaptor, ZoneDetectionAdaptor, WindowTrackingAdaptor,
    // DBusScreenAdaptor, WindowDragAdaptor, CompositorBridgeAdaptor,
    // SnapAdaptor, AutotileAdaptor) all ship destructors that don't
    // deref any borrowed pointer — most are `= default` / empty-body
    // (no member access), and the two outliers do only self-cleanup
    // on a Qt-child member: DBusScreenAdaptor ships an empty out-of-
    // line body, and WindowTrackingAdaptor's `~WindowTrackingAdaptor`
    // calls `m_service->setShouldTrackPredicate({})` on its Qt-child
    // m_service to clear a captured-this lambda before the child
    // tears down (see Pass-3 commit c4e3c5125). The substantive
    // safety claim is "no borrowed-pointer deref runs in any of their
    // destructors" — confirmed by inspecting each header + cpp pair,
    // not header alone. QDBusConnection::unregisterObject (invoked above) blocks new
    // method dispatch to them before we begin tearing down, and Qt's
    // sender-destruction auto-disconnect cleans up signal wiring when the
    // borrowed sender (m_layoutManager, etc.) is destroyed during member
    // destruction. Adding detach() to those nine would require null-guarding
    // every slot body (they currently rely on the "borrowed pointer is
    // always valid" invariant), which is a larger refactor than the
    // defense-in-depth buys. If a future adaptor grows a dtor body that
    // derefs a borrowed member, add detach() to it AND wire the call here
    // — same pattern as these four.
    if (m_settingsAdaptor) {
        m_settingsAdaptor->detach();
    }
    if (m_shaderAdaptor) {
        m_shaderAdaptor->detach();
    }
    if (m_controlAdaptor) {
        m_controlAdaptor->detach();
    }
    if (m_windowRuleAdaptor) {
        m_windowRuleAdaptor->detach();
    }

    // Provider lambdas already cleared at the top of stop() (before the
    // m_running gate) so this point requires no further teardown.

    m_running = false;
}

void Daemon::queryPlasmaWorkspaceState()
{
    // Query the user-bus systemd for `plasma-workspace.target`'s ActiveState
    // to distinguish a real Plasma session from a phantom plasma-restore session.
    //
    // During user-logout → SDDM handoff, systemd may respawn the daemon into a
    // transient "phantom" state: a stray `kwin_wayland` from a fallback session-
    // restore mechanism briefly publishes a fresh `wayland-N` socket inside the
    // still-dying `user@.service`, and `Restart=on-failure` schedules a daemon
    // retry after Qt's wayland QPA aborts on the vanished `wl_display`. The
    // phantom daemon fires welcome OSDs against an output about to be unbound.
    //
    // Why this signal works: `plasma-workspace.target` is only flipped to `active`
    // by `startplasma-wayland`'s orchestration after SDDM hands off. The phantom
    // has no `startplasma-wayland` leader, so the target stays inactive — a signal
    // the phantom cannot fake. logind's `User.State` stays `active` whenever
    // `user@.service` is up (can't distinguish phantom from real), and the
    // wayland-socket existence probe passes during the phantom.
    //
    // Fail-open on all D-Bus errors: `m_plasmaWorkspaceActive` defaults to `true`,
    // so non-systemd setups and headless tests aren't accidentally silenced.
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    if (!sessionBus.isConnected()) {
        qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: session bus unavailable, leaving m_plasmaWorkspaceActive=true";
        return;
    }

    QDBusMessage subscribeMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("Subscribe"));
    auto* subscribeWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(subscribeMsg), this);
    connect(subscribeWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: Subscribe failed:" << reply.error().message()
                              << "— PropertiesChanged signals may not arrive";
        }
    });

    QDBusMessage getUnitMsg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
        QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("GetUnit"));
    getUnitMsg << QStringLiteral("plasma-workspace.target");
    auto* getUnitWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(getUnitMsg), this);
    connect(getUnitWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError()) {
            qCInfo(lcDaemon) << "queryPlasmaWorkspaceState: GetUnit('plasma-workspace.target') failed:"
                             << reply.error().message() << "— leaving fail-open (target not loaded)";
            return;
        }
        m_plasmaWorkspaceTargetPath = reply.value().path();
        if (m_plasmaWorkspaceTargetPath.isEmpty()) {
            return;
        }
        fetchPlasmaWorkspaceActiveState();
    });
}

void Daemon::fetchPlasmaWorkspaceActiveState()
{
    QDBusConnection sessionBus = QDBusConnection::sessionBus();
    QDBusMessage msg =
        QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), m_plasmaWorkspaceTargetPath,
                                       QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    msg << QStringLiteral("org.freedesktop.systemd1.Unit") << QStringLiteral("ActiveState");
    auto* watcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(msg), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        QDBusPendingReply<QVariant> reply = *w;
        if (reply.isError()) {
            qCDebug(lcDaemon) << "queryPlasmaWorkspaceState: ActiveState Get failed:" << reply.error().message();
            return;
        }
        const QString state = reply.value().toString();
        m_plasmaWorkspaceActive = (state == QLatin1String("active"));
        qCInfo(lcDaemon) << "plasma-workspace.target ActiveState at startup:" << state
                         << "plasmaWorkspaceActive=" << m_plasmaWorkspaceActive
                         << "path=" << m_plasmaWorkspaceTargetPath;

        QDBusConnection bus = QDBusConnection::sessionBus();
        const bool ok =
            bus.connect(QStringLiteral("org.freedesktop.systemd1"), m_plasmaWorkspaceTargetPath,
                        QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPlasmaWorkspaceTargetPropertiesChanged(QString, QVariantMap, QStringList)));
        if (!ok) {
            qCWarning(lcDaemon) << "queryPlasmaWorkspaceState: failed to subscribe to Unit PropertiesChanged on"
                                << m_plasmaWorkspaceTargetPath;
        }
    });
}

void Daemon::onPlasmaWorkspaceTargetPropertiesChanged(const QString& interfaceName,
                                                      const QVariantMap& changedProperties,
                                                      const QStringList& /*invalidatedProperties*/)
{
    if (interfaceName != QLatin1String("org.freedesktop.systemd1.Unit")) {
        return;
    }
    const auto it = changedProperties.constFind(QStringLiteral("ActiveState"));
    if (it == changedProperties.constEnd()) {
        return;
    }
    const QString state = it->toString();
    const bool nowActive = (state == QLatin1String("active"));
    if (m_plasmaWorkspaceActive != nowActive) {
        qCInfo(lcDaemon) << "plasma-workspace.target state changed:" << state;
    }
    m_plasmaWorkspaceActive = nowActive;
}

} // namespace PlasmaZones
