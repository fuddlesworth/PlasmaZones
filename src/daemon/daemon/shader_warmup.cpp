// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "daemon/daemon.h"
#include "helpers.h"

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
#include <array>

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

#include "daemon/overlayservice.h"
#include "daemon/controllers/unifiedlayoutcontroller.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/enginefactory.h"
#include "daemon/controllers/contextresolverwiring.h"
#include "daemon/rendering/surfaceshaderitem.h"
#include "daemon/rendering/zoneentryscaffold.h"
#include "daemon/rendering/zoneshadernoderhi.h"

#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/ZonesLayoutSource.h>
#include <PhosphorZones/LayoutComputeService.h>
#include <PhosphorZones/ZoneDetector.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/AutotileLayoutSourceFactory.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorContext/ContextResolver.h>
#include <PhosphorScreens/DBusScreenAdaptor.h>
#include <PhosphorScreens/Swapper.h>
#include <PhosphorScreens/PlasmaPanelSource.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorSnapEngine/SnapEngine.h>
#include <PhosphorSnapEngine/SnapState.h>
#include <PhosphorTileEngine/AutotileEngine.h>
#include <PhosphorRules/ExclusionRules.h>
#include <PhosphorRules/RuleAction.h>
#include <PhosphorRules/Rule.h>
#include <PhosphorRules/RuleStore.h>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settingsconfigstore.h"
#include "config/settings.h"
#include "core/types/baselinecleanup.h"
#include "core/types/constants.h"
#include "core/resolve/crosssurfaceresolver.h"
#include "core/resolve/animationbootstrap.h"
#include "core/resolve/screenmoderouter.h"
#include "core/utils/geometryutils.h"
#include "core/utils/utils.h"
#include "core/platform/logging.h"
#include "core/interfaces/shaderregistry.h"
#include "common/screenidresolver.h"
#include "common/layoutbundlebuilder.h"
#include "phosphor_i18n.h"
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

namespace PlasmaZones {

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
    // silently dropped. Triggered explicitly by the three-phase load
    // further down.
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
        // The loader has replaced its owned entries, so the cached raw JSON
        // profiles are stale. Drop them before republishing; the publish
        // re-snapshots from the registry, which holds the freshly parsed
        // entries at this point.
        m_rawJsonProfiles.clear();
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
    // User-wins at the registry level: if the ProfileLoader owns a
    // user-authored JSON file at a settings-driven path, its set fields
    // win and its UNSET fields merge from the user's settings (never from
    // library defaults) — see the per-path ownership + merge logic below.
    // On JSON delete, the loader emits profilesChanged, this function
    // re-runs, and the settings-default path is restored.
    //
    // This runs on the settings-slider hot path (~30 Hz during drag), so
    // ownership is resolved with an O(1) `ownerOf()` lookup rather than
    // `entries()`, which copies and sorts the full tracked set every tick.
    auto& reg = m_profileRegistry;

    const Profile settingsProfile = m_settings->animationProfile();
    for (const QString* path : kSettingsDrivenProfilePaths) {
        // OWNERSHIP, not existence. hasPath() asks the loader's own bookkeeping
        // whether it parsed a file for this path. That stays true even when the
        // registry entry is something else entirely — including this function's
        // OWN untagged publish from a previous tick. Merging over that is
        // self-poisoning: the settings profile has every field engaged, so
        // nothing falls back, the entry freezes, and every later slider move is
        // silently dropped until the daemon restarts.
        //
        // Two ways in, both previously live. A registry/loader disagreement,
        // and — with no invariant violated at all — a user dropping a
        // Global.json into a session that already published untagged, where
        // reloadFromOwner's "direct owner always wins" rule makes the loader
        // skip the path while still emitting profilesChanged.
        //
        // ownerOf() answers the question that actually matters: is this entry
        // the loader's parsed JSON? Only then is it a valid merge base.
        //
        // Resolved ONCE per path: the tag is needed again at the re-register
        // below, and this is a ~30 Hz path where each ownerOf() is a locked
        // lookup.
        const QString pathOwner = reg.ownerOf(*path);
        const bool loaderOwnsPath = m_profileLoader && pathOwner == m_profileLoader->ownerTag();
        if (loaderOwnsPath) {
            // A user JSON owns this path, but its unset fields must still fall
            // back to the user's settings rather than to library defaults.
            // Skipping wholesale left a Global.json that set only `duration`
            // animating minDistance / sequenceMode / staggerInterval / curve at
            // built-in defaults, while the settings app resolved them from
            // ISettings and showed the user's values.
            //
            // Merge from a cached RAW snapshot taken once per loader reload,
            // never from the registry's current entry — that is the merged
            // result of the previous tick, and reading it back is the freeze
            // described above.
            auto rawIt = m_rawJsonProfiles.constFind(*path);
            if (rawIt == m_rawJsonProfiles.constEnd()) {
                const auto owned = reg.resolve(*path);
                if (!owned.has_value()) {
                    // ownerOf() named the loader, so the entry exists by
                    // construction. Belt and braces.
                    //
                    // Republished under the LOADER's tag, not untagged. An
                    // untagged entry is a direct-owner entry, and
                    // reloadFromOwner's "direct owner always wins" rule then
                    // makes the loader skip this path on every later rescan —
                    // the user's JSON would be silently discarded for the rest
                    // of the session, which is exactly what the ownership check
                    // above exists to prevent.
                    qCWarning(lcCore) << "animation profile publish: registry reports loader ownership of" << *path
                                      << "but has no entry — publishing settings defaults instead";
                    reg.registerProfile(*path, settingsProfile, pathOwner);
                    continue;
                }
                rawIt = m_rawJsonProfiles.insert(*path, *owned);
            }

            Profile mergedProfile = rawIt.value();
            if (!mergedProfile.duration.has_value())
                mergedProfile.duration = settingsProfile.duration;
            if (!mergedProfile.curve)
                mergedProfile.curve = settingsProfile.curve;
            if (!mergedProfile.minDistance.has_value())
                mergedProfile.minDistance = settingsProfile.minDistance;
            if (!mergedProfile.sequenceMode.has_value())
                mergedProfile.sequenceMode = settingsProfile.sequenceMode;
            if (!mergedProfile.staggerInterval.has_value())
                mergedProfile.staggerInterval = settingsProfile.staggerInterval;
            if (!mergedProfile.presetName.has_value())
                mergedProfile.presetName = settingsProfile.presetName;
            // Under the JSON's own owner tag, so the loader's next
            // reloadFromOwner still replaces it.
            //
            // No pre-check: registerProfile already compares BOTH value and
            // owner before inserting or emitting, so a guard here would be
            // dead, would cost an extra locked resolve() call on a ~30 Hz
            // path, and — comparing value only — would miss an owner-only
            // difference that registerProfile does correct.
            reg.registerProfile(*path, mergedProfile, pathOwner);
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

void Daemon::setupSurfaceShaderEffects()
{
    m_surfaceShaderRegistry = std::make_unique<PhosphorSurfaceShaders::SurfaceShaderRegistry>(nullptr);

    // System dirs from XDG_DATA_DIRS in descending priority. Reverse so the
    // first registered is the lowest-priority system dir — the loader applies
    // first-registration-wins, yielding `user > sys-highest > ... > sys-lowest`
    // after the user dir is appended last. Surface packs install to
    // `plasmazones/surface` (singular), the third category beside
    // `plasmazones/overlays` and `plasmazones/animations`.
    QStringList surfaceDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/surface"), QStandardPaths::LocateDirectory);
    std::reverse(surfaceDirs.begin(), surfaceDirs.end());

    const QString userSurfaceDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/plasmazones/surface");
    if (!surfaceDirs.contains(userSurfaceDir))
        surfaceDirs.append(userSurfaceDir);

    // Materialise the user dir BEFORE registering so the watcher attaches a
    // direct watch instead of a parent-watch proxy (mirrors the animation /
    // curve / profile setup). Failures are non-fatal — the on-demand scan still
    // runs without a watch.
    QDir().mkpath(userSurfaceDir);

    m_surfaceShaderRegistry->setUserPath(userSurfaceDir);
    m_surfaceShaderRegistry->addSearchPaths(surfaceDirs);

    // Stage d: hand the registry to the overlay service so the OSD show path can
    // resolve its decoration pack's fragment shader + translated params. Borrow
    // is nulled in stop() before the registry is reset, mirroring the animation
    // registry's teardown.
    if (m_overlayService) {
        m_overlayService->setSurfaceShaderRegistry(m_surfaceShaderRegistry.get());
    }
}

void Daemon::setupShaderWarmBakes()
{
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

    // Warm-bake SURFACE shaders (window border / rounded corners / glow) the
    // same way zone + animation shaders are warmed above, so the first surface
    // paint never blocks the render thread on a cold glslang compile. Shares the
    // single-threaded m_shaderBakePool, so all three categories serialise.
    //
    // Like the zone/animation warm-bakes, the surface bake installs the same
    // fragment entry-point scaffold the live loader uses (the
    // surfaceEntryPrologue()/surfaceEntryCandidates() passed below), so a
    // pSurface()-only pack compiles and the warm key matches SurfaceShaderItem's
    // scaffolded live load. The one thing that differs is vertex-shader
    // resolution: surface packs ship NO vertex shader, so the vert is resolved
    // from a shared `surface.vert` in the include paths (same resolution as
    // SurfaceShaderItem). A pack with no resolvable vert is skipped — the live
    // path would error on it too, so there is nothing to warm.
    if (m_surfaceShaderRegistry) {
        auto scheduleWarmForSurfaceEffect = [this,
                                             registryPtr = QPointer<PhosphorSurfaceShaders::SurfaceShaderRegistry>(
                                                 m_surfaceShaderRegistry.get())](
                                                const PhosphorSurfaceShaders::SurfaceShaderEffect& info) {
            if (!info.isValid() || info.fragmentShaderPath.isEmpty() || !QFile::exists(info.fragmentShaderPath)) {
                return;
            }
            // Pure liveness gate: the include paths come from the static
            // helper below (not the registry), so the QPointer is checked
            // only to skip bakes scheduled across registry teardown.
            if (!registryPtr) {
                return;
            }
            // Include paths come from the SAME function the live loader's
            // constructor uses (SurfaceShaderItem::surfaceIncludePaths — XDG
            // dirs user-first, each contributing its `shared` subdir then
            // itself), so the bake-cache key the warm compile writes is
            // structurally guaranteed to be the one the first live paint
            // looks up; the two can no longer silently diverge. The vert
            // resolves as the live loader does: the pack's metadata vert
            // first, else `surface.vert` beside the frag, else the first
            // `surface.vert` in the include dirs (the `shared` subdir
            // carries the shipped one — packs themselves ship no vert, so
            // both sides land on the shared vert today). The overlay host
            // satisfies the matching side of this contract: applyDecoration
            // writes each chain stage's vertexSource from the pack's declared
            // vertexShaderPath (and the registry preamble), so a pack that
            // ships its own vert keys the same vert on both the warm bake
            // and the live load.
            const QStringList includePaths = SurfaceShaderItem::surfaceIncludePaths();
            QString vertPath = info.vertexShaderPath;
            if (vertPath.isEmpty()) {
                const QString besideFrag =
                    QFileInfo(info.fragmentShaderPath).absolutePath() + QStringLiteral("/surface.vert");
                if (QFile::exists(besideFrag)) {
                    vertPath = besideFrag;
                }
            }
            if (vertPath.isEmpty()) {
                for (const QString& incDir : std::as_const(includePaths)) {
                    const QString candidate = incDir + QStringLiteral("/surface.vert");
                    if (QFile::exists(candidate)) {
                        vertPath = candidate;
                        break;
                    }
                }
            }
            if (vertPath.isEmpty() || !QFile::exists(vertPath)) {
                return;
            }
            const QString effectId = info.id;
            const QString paramPreamble = PhosphorSurfaceShaders::SurfaceShaderRegistry::paramPreamble(info);
            // Bake WITH the same fragment entry-point scaffold the live loader
            // installs (surfaceshaderitem.cpp setEntryScaffold), or a
            // `pSurface()`-only pack bakes its raw main()-less source and fails
            // to compile, and even a traditional main() pack would key the
            // cache differently from the scaffolded live load and miss it.
            // Mirrors the zone and animation warm-bakes above.
            const QString entryPrologue = PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryPrologue();
            const QList<PhosphorShaders::EntryCandidate> entryCandidates =
                PhosphorSurfaceShaders::SurfaceShaderRegistry::surfaceEntryCandidates();
            auto* watcher = new QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>(this);
            connect(watcher, &QFutureWatcher<PhosphorRendering::WarmShaderBakeResult>::finished, this,
                    [watcher, effectId]() {
                        const PhosphorRendering::WarmShaderBakeResult r = watcher->result();
                        if (!r.success) {
                            qCWarning(lcDaemon) << "Surface shader bake: failed for" << effectId << r.errorMessage;
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
        connect(m_surfaceShaderRegistry.get(), &PhosphorSurfaceShaders::SurfaceShaderRegistry::effectsChanged, this,
                [this, scheduleWarmForSurfaceEffect]() {
                    if (!m_surfaceShaderRegistry) {
                        return;
                    }
                    const QList<PhosphorSurfaceShaders::SurfaceShaderEffect> effects =
                        m_surfaceShaderRegistry->availableEffects();
                    for (const PhosphorSurfaceShaders::SurfaceShaderEffect& info : effects) {
                        scheduleWarmForSurfaceEffect(info);
                    }
                });
        for (const PhosphorSurfaceShaders::SurfaceShaderEffect& info : m_surfaceShaderRegistry->availableEffects()) {
            scheduleWarmForSurfaceEffect(info);
        }
    }
}

} // namespace PlasmaZones
