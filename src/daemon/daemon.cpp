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
#include <QDBusError>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QThread>

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/qml/PhosphorCurve.h>
#include <PhosphorAnimation/qml/QtQuickClockManager.h>

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <array>

#include "overlayservice.h"
#include "modetracker.h"
#include "shortcutmanager.h"
#include "rendering/zoneshadernoderhi.h"
#include <PhosphorZones/LayoutRegistry.h>
#include "../config/configbackends.h"
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
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
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
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
    , m_settings(std::make_unique<Settings>(m_configBackend.get(), &m_curveRegistry, nullptr))
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
    , m_overlayService(
          std::make_unique<OverlayService>(m_screenManager.get(), m_shaderRegistry.get(), &m_profileRegistry, nullptr))
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
    // instance. The registry is a daemon-owned member, so a fresh
    // Daemon construction always starts with an empty registry — but
    // setupAnimationProfiles is also called on configure-reload paths
    // where prior loaders may have populated the partitions; clearing
    // here keeps the post-condition uniform.
    //
    // Narrow the clear to the two partitions we publish under: the
    // loader-owned user-JSON partition (clearOwner by tag) and each
    // individual settings-driven path (unregisterProfile per path).
    // Wholesale `clear()` would also evict any other consumer's
    // entries if they happened to register before us — not a concern
    // in production today but the narrower scope is the correct
    // contract for a registry that may be shared with other consumers
    // in future multi-publisher configurations.
    PhosphorProfileRegistry& registry = m_profileRegistry;
    registry.clearOwner(kPlasmaZonesUserProfilesOwnerTag);
    for (const QString* path : kSettingsDrivenProfilePaths) {
        registry.unregisterProfile(*path);
    }

    // User-authored curve + profile packs. Consumer namespace is
    // `plasmazones/` per the LayoutManager precedent — library-level
    // CurveLoader / ProfileLoader are agnostic (decision U); the daemon
    // picks PlasmaZones's namespace here.
    //
    // `QStandardPaths::locateAll` returns directories in priority order —
    // the writable user location FIRST, system dirs AFTER. The loader
    // iterates in caller-supplied order and lets later entries override
    // earlier on key collision, so we reverse to achieve the standard
    // "system first, user wins last" layering (decision X). Matches
    // LayoutManager::loadLayouts (src/core/layoutmanager/persistence.cpp).
    QStringList curveDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/curves"), QStandardPaths::LocateDirectory);
    std::reverse(curveDirs.begin(), curveDirs.end());
    QStringList profileDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/profiles"), QStandardPaths::LocateDirectory);
    std::reverse(profileDirs.begin(), profileDirs.end());

    // locateAll returns an empty list when none of the candidate dirs
    // exist yet (fresh install). Unconditionally include the writable
    // location so the loader can watch it via the parent-directory
    // fallback — once the user drops a file there, the watcher fires
    // and the loader picks it up without a daemon restart.
    const QString userCurveDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/curves");
    if (!curveDirs.contains(userCurveDir)) {
        curveDirs.append(userCurveDir);
    }
    const QString userProfileDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/profiles");
    if (!profileDirs.contains(userProfileDir)) {
        profileDirs.append(userProfileDir);
    }

    // Materialise the user dirs eagerly so live-reload works on fresh
    // installs. `WatchedDirectorySet`'s parent-watch climb refuses to
    // attach a `QFileSystemWatcher` to forbidden ancestors (`$HOME`,
    // `$XDG_DATA_HOME`, etc.) — without these dirs existing, the climb
    // terminates at `~/.local/share` and NO watch is installed. The
    // user could then drop `~/.local/share/plasmazones/profiles/foo.json`
    // and live-reload would silently never fire until daemon restart.
    // Mirrors `ScriptedAlgorithmLoader::ensureUserDirectoryExists` and
    // the ShaderRegistry ctor's `mkpath`. Failures are non-fatal — the
    // initial on-demand scan still works without a watch.
    QDir().mkpath(userCurveDir);
    QDir().mkpath(userProfileDir);

    // Construct loaders with NO initial load — the loadFromDirectories
    // calls below trigger the first scan AFTER every consumer signal is
    // wired. A loader's initial-scan profilesChanged emit otherwise fires
    // before the publishActiveAnimationProfile listener exists and is
    // silently dropped — we'd still get a correct snapshot via the
    // explicit publish call at the bottom, but the signal-driven path
    // would be quietly broken until something else triggered a republish.
    //
    // Registry reference is captured at loader construction — this
    // prevents any later async rescan from landing on a different
    // registry than the one the daemon initialized against.
    //
    // ProfileLoader is bound to PhosphorProfileRegistry with a
    // PlasmaZones-specific owner tag. Entries it registers are tagged
    // `kPlasmaZonesUserProfilesOwnerTag` in the registry's partitioned-ownership
    // map, so a rescan replaces ONLY the user's JSON-backed profiles;
    // the daemon's settings-fanned profiles registered below via
    // `publishActiveAnimationProfile` are owned by the empty/direct
    // tag and survive untouched.
    m_curveLoader = std::make_unique<CurveLoader>(m_curveRegistry, nullptr);
    m_profileLoader =
        std::make_unique<ProfileLoader>(m_profileRegistry, m_curveRegistry, kPlasmaZonesUserProfilesOwnerTag, nullptr);

    // Wire CurveLoader::curvesChanged → ProfileLoader rescan. A Profile
    // whose `curve` spec references a user-authored curve name that
    // wasn't yet in CurveRegistry at parse time is stored with
    // `curve = nullptr` (falls back to library default at animation
    // time). When the user drops or edits a curve JSON AFTER the
    // profile files were scanned, we need to re-parse every profile
    // so the newly-available curve gets resolved. Without this wire,
    // drop-order-matters: curves-before-profiles works, profiles-
    // before-curves silently loses the curve reference until the
    // profile file itself is touched.
    //
    // ProfileLoader::requestRescan goes through DirectoryLoader's
    // debounced rescan path, so a curve-pack edit that changes many
    // files coalesces into one profile rescan.
    connect(m_curveLoader.get(), &CurveLoader::curvesChanged, m_profileLoader.get(), &ProfileLoader::requestRescan);

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

    // Initial scans — order is curves-before-profiles so profile files
    // referencing user-authored curve names resolve on first parse
    // rather than waiting for the curveLoader→profileLoader rescan wire
    // to fire on the second pass.
    //
    // Library-level pack first (today a no-op — the library ships no
    // bundled curves/profiles — but kept for future curve-pack additions).
    m_curveLoader->loadLibraryBuiltins();
    m_curveLoader->loadFromDirectories(curveDirs, LiveReload::On);

    m_profileLoader->loadLibraryBuiltins();
    m_profileLoader->loadFromDirectories(profileDirs, LiveReload::On);

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

    // Mark the user dir BEFORE the initial scan so discovered effects'
    // `isUserEffect` flag is set correctly on first commit. Settings UI
    // / QML pickers consume this to render the "user" badge — without
    // the explicit mark, every effect would surface as system.
    m_animationShaderRegistry->setUserShaderPath(userAnimDir);

    // Single batched register — the underlying WatchedDirectorySet runs
    // ONE synchronous scan for the whole batch, populates `m_effects`
    // via the strategy, and emits `effectsChanged` once if non-empty.
    // No follow-up `refresh()` — the registration path already scanned.
    m_animationShaderRegistry->addSearchPaths(animDirs);
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

    connect(autotileEngine, &PhosphorEngineApi::PlacementEngineBase::settingsPersistRequested, this, [this]() {
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
    snapEngine->setNavigationStateProvider(m_windowTrackingAdaptor);

    // Clear stale autotile-floated flag when a window is snapped. A window
    // dragged from an autotile VS to a snap VS retains its autotileFloated
    // marker; without this, a subsequent mode change on the autotile VS
    // incorrectly processes the already-snapped window as autotile-managed.
    // Wired here (daemon) because engines must not know about each other.
    connect(snapEngine, &PhosphorSnapEngine::SnapEngine::windowSnapStateChanged, this,
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

            // Show OSD for changed screens — use locked OSD variant when context is locked.
            // KCM Apply is an explicit user-driven layout assignment change, so the regular
            // preview OSDs gate on showOsdOnLayoutSwitch (matching cycle / quick-layout /
            // zone-selector-drop). The locked-context OSD bypasses the toggle by design — it
            // explains why a requested change had no visible effect on that screen, the same
            // pattern used for the mode-toggle locked feedback in connectShortcutSignals().
            const bool osdEnabled = m_settings && m_settings->showOsdOnLayoutSwitch();
            for (const auto& osd : std::as_const(osdEntries)) {
                int mode = osd.isAutotile ? 1 : 0;
                if (isCurrentContextLockedForMode(osd.screenId, mode)) {
                    showLockedPreviewOsd(osd.screenId);
                } else if (!osdEnabled) {
                    continue;
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

    // Intentionally last: the algorithmChanged handler (signals.cpp) and showDesktopSwitchOsd
    // (osd.cpp) both gate on !m_running to suppress OSD/feedback during startup. finalizeStartup()
    // calls m_autotileEngine->loadState() which synchronously emits algorithmChanged, and
    // KWin/Plasma can deliver desktop/activity-change signals during the same window. Setting
    // m_running before finalizeStartup() returns would let those handlers fire and double-queue
    // (or leak past) the startup OSD that finalizeStartup() is responsible for.
    m_running = true;
    // NOTE: daemonReady() is emitted by finalizeStartup() — do NOT emit again here.
}

void Daemon::stop()
{
    if (!m_running) {
        return;
    }

    // Null the QML static registry / manager pointers before our owned
    // members are destroyed (unique_ptr / value-typed member destruction
    // runs AFTER ~Daemon body completes — tearing the static borrowed
    // pointers now guarantees no QML callsite landing during teardown
    // or in a subsequent Daemon instance dereferences freed memory.
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(nullptr);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(nullptr);

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
        m_layoutManager->setDefaultAutotileAlgorithmProvider({});
    }

    m_running = false;
}

} // namespace PlasmaZones
