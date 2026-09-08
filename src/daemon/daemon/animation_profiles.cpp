// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Daemon animation-profile composition: which paths follow the user's
// `Settings.animationProfile` slider, how the user profiles directory is
// seeded and watched, and how the active profile is republished to the
// compositor when settings or the profile registry move.
//
// Split out of `shader_warmup.cpp`, which is about baking shader pipelines.
// The two shared a file only because both run during daemon start-up; nothing
// here touches a shader. Same class, separate TU, no API change.

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "core/resolve/animationbootstrap.h"

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/QtQuickClockManager.h>

#include <QLatin1StringView>
#include <QString>
#include <QTimer>

#include <array>
#include <memory>
#include <utility>

namespace PlasmaZones {

// Paths that follow the user's `Settings.animationProfile` slider directly.
//
// `global` is the root of every other path's chain, so what this publish does
// to the rest of the tree depends on WHICH LAYER it lands in, and that is
// decided in publishActiveAnimationProfile by whether the user has actually
// written the global blob. Unset, it joins the family seeds and the deeper
// per-family seeds override it. Set, it outranks them, which is what a
// "retime everything" control has to mean. Either way the user's per-event
// overrides from `Animations/MotionProfileTree` sit above both.
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

/// Owner tag used to partition the user's per-event TIMING overrides, read
/// from `Animations/MotionProfileTree`. Lives in the registry's
/// partitioned-ownership map so replacing that partition on a settings change
/// wipes only these entries, leaving the settings-driven ones (owned by the
/// empty/direct tag) and the family seeds alone.
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
    // Narrow the clear to the three partitions we publish under: the
    // loader-owned user-JSON partition (clearOwner by tag), the shell
    // animation-family seed partition (clearOwner by its tag), and each
    // individual settings-driven path (unregisterProfile per path).
    // Wholesale `clear()` would also evict any other consumer's
    // entries if they happened to register before us — not a concern
    // in production today but the narrower scope is the correct
    // contract for a registry that may be shared with other consumers.
    PhosphorProfileRegistry& registry = m_profileRegistry;
    registry.clearOwner(QString(kPlasmaZonesUserProfilesOwnerTag));
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

    // Discover the XDG `plasmazones/curves` dirs, materialise the
    // user-writable ones, and construct the curve loader. Shared with the
    // secondary composition roots (settings / editor) via
    // `AnimationBootstrap`, so dir discovery and loader construction exist in
    // one place.
    //
    // The initial `loadFromDirectories` scan is deferred until AFTER the
    // daemon's pre-scan signal wiring below — a loader's initial-scan emit
    // otherwise fires before the publishActiveAnimationProfile listener is
    // installed and is silently dropped. Triggered explicitly by the
    // three-phase load further down.
    //
    // There is no per-event profile LOADER any more. Since schema v8 an
    // event's timing lives beside its pack in config, under
    // `Animations/MotionProfileTree`, and reaches the registry through
    // `installMotionProfileTree` below.
    auto loaderHandles = constructAnimationLoaders(m_curveRegistry, nullptr);
    m_curveLoader = std::move(loaderHandles.curveLoader);
    const AnimationLoaderDirs loaderDirs = std::move(loaderHandles.dirs);

    // Connect BEFORE the initial scans below so any signal Settings
    // fires during load is captured. The registry's value-changed
    // guard makes the subsequent publishActiveAnimationProfile a no-op
    // if the signal-driven path already published the same values.
    //
    // Re-publish on:
    //   - Settings edits (slider drag, per-field setter) — the aggregate
    //     animationProfileChanged signal fires.
    //   - Per-event timing edits — motionProfileTreeChanged fires when the
    //     user adds or clears an override, which flips the hasProfile()
    //     check for that path.
    //   - CurveLoader rescans — a curve JSON referenced by the
    //     settings-driven Global profile changed on disk. Settings
    //     ::animationProfile() reparses the stored blob through
    //     CurveRegistry on every call (no cache), so republishing
    //     re-resolves the curve against the fresh registry state.
    //     Without this wire the Global slider's curve reference would
    //     silently go stale until the next Settings edit.
    // All three signals route through `requestAnimationProfilePublish`
    // — a coalescing trampoline that collapses every fan-in within the
    // same event-loop tick into exactly one `publishActiveAnimationProfile`
    // call. The settings-slider drag on its own fires the aggregate at
    // ~30 Hz, and one user action can fire two of these in the same tick —
    // without coalescing, the publish path's Settings parse and curve resolve
    // would run once per signal rather than once per tick.
    m_animationPublishTimer.setSingleShot(true);
    m_animationPublishTimer.setInterval(0);
    connect(&m_animationPublishTimer, &QTimer::timeout, this, [this]() {
        m_animationPublishPending = false;
        publishActiveAnimationProfile();
    });
    connect(m_settings.get(), &Settings::animationProfileChanged, this, [this]() {
        requestAnimationProfilePublish();
    });
    connect(m_settings.get(), &Settings::motionProfileTreeChanged, this, [this]() {
        // The per-event timing tree moved. Re-register this owner's whole
        // partition (a cleared path must LEAVE the registry, which is why
        // installMotionProfileTree replaces rather than merges) and drop the
        // cached raw profiles the publish path re-snapshots from.
        installMotionProfileTree(m_profileRegistry, m_curveRegistry, m_settings->motionProfileTree(),
                                 QString(kPlasmaZonesUserProfilesOwnerTag));
        m_rawJsonProfiles.clear();
        requestAnimationProfilePublish();
    });
    connect(m_curveLoader.get(), &CurveLoader::curvesChanged, this, [this]() {
        // Same staleness rule as the profilesChanged handler above: a cached
        // raw profile may hold a Profile::curve pointer resolved against the
        // pre-edit curve. Self-correcting even without this (curvesChanged is
        // also wired to the profile loader's debounced rescan, whose
        // profilesChanged clears the cache), but that leaves one publish tick
        // serving the stale curve; the clear is free.
        //
        // The timing tree is re-installed for the same reason: each Profile
        // holds the curve it RESOLVED at parse time, so an entry that named a
        // curve which did not exist yet stores a null curve and silently
        // animates on the library default until the tree is parsed again.
        installMotionProfileTree(m_profileRegistry, m_curveRegistry, m_settings->motionProfileTree(),
                                 QString(kPlasmaZonesUserProfilesOwnerTag));
        m_rawJsonProfiles.clear();
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

    // Three-phase initial load — curves first so the family-seed step and the
    // timing tree can both resolve named curves like `widget-out`; family
    // seeds next; the config-backed per-event timing tree last, so a user
    // override at a seeded path takes the slot. The order mirrors
    // AnimationBootstrap so secondary composition roots get the same shape.
    runInitialCurveLoad(*m_curveLoader, loaderDirs);
    seedShellAnimationFamilies(m_profileRegistry, m_curveRegistry);
    installMotionProfileTree(m_profileRegistry, m_curveRegistry, m_settings->motionProfileTree(),
                             QString(kPlasmaZonesUserProfilesOwnerTag));

    // Final explicit publish covers the case where neither the Settings
    // nor the timing-tree install emitted during the loads above (e.g. fresh
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

    // Publish the settings-driven paths (Global). Every OTHER path is served
    // by the family seeds plus the user's per-event overrides from
    // `Animations/MotionProfileTree`. `registerProfile` has an equality guard
    // so re-publishing identical values on every settingsChanged signal is a
    // cheap no-op on the hot path.
    //
    // User-wins at the registry level: if the timing tree carries an override
    // at a settings-driven path, its set fields win and its UNSET fields merge
    // from the user's settings, never from library defaults — see the per-path
    // ownership and merge logic below. Clearing that override re-runs this
    // function and restores the settings-default path.
    //
    // SCOPE of that contract: it holds when the override is present at the
    // first install (setup installs the tree before the first untagged
    // publish). An override that appears AT RUNTIME at a settings-driven path,
    // in a session that already published untagged, is deliberately NOT
    // adopted: reloadFromOwner's "direct owner always wins" rule makes the
    // tree step aside for the untagged entry, so it takes effect on the next
    // daemon start. Nothing in the UI writes one — `global` is not an event
    // path — so this is a hand-edited-config case only.
    //
    // This runs on the settings-slider hot path (~30 Hz during drag), so
    // ownership is resolved with an O(1) `ownerOf()` lookup rather than
    // `entries()`, which copies and sorts the full tracked set every tick.
    auto& reg = m_profileRegistry;

    const Profile settingsProfile = m_settings->animationProfile();
    // Read once per publish, not per path: it is a backend key lookup and
    // this runs at slider-drag rate.
    const bool settingsExplicit = m_settings->hasExplicitAnimationProfile();
    for (const QString* path : kSettingsDrivenProfilePaths) {
        // OWNERSHIP, not existence. Asking whether the timing tree CONTAINS
        // this path answers the wrong question: it stays true even when the
        // registry entry is something else entirely, including this function's
        // OWN untagged publish from a previous tick. Merging over that is
        // self-poisoning: the settings profile has every field engaged, so
        // nothing falls back, the entry freezes, and every later slider move is
        // silently dropped until the daemon restarts.
        //
        // ownerOf() answers the question that actually matters: is this entry
        // the one the tree install put here? Only then is it a valid merge
        // base.
        //
        // Resolved ONCE per path: the tag is needed again at the re-register
        // below, and this is a ~30 Hz path where each ownerOf() is a locked
        // lookup.
        const QString pathOwner = reg.ownerOf(*path);
        const bool treeOwnsPath = pathOwner == QString(kPlasmaZonesUserProfilesOwnerTag);
        if (treeOwnsPath) {
            // The tree owns this path, but its unset fields must still fall
            // back to the user's settings rather than to library defaults.
            // Skipping wholesale left an override that set only `duration`
            // animating minDistance / sequenceMode / staggerInterval / curve at
            // built-in defaults, while the settings app resolved them from
            // ISettings and showed the user's values.
            //
            // Merge from a cached RAW snapshot taken once per tree install,
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
                    // Seed an EMPTY raw-JSON base for this path. Without it the
                    // next publish tick misses the raw cache again, re-resolves,
                    // and caches the settings profile we just wrote as the "raw
                    // JSON" merge base — every field then reads as user-authored
                    // and later slider moves are silently dropped. An empty base
                    // makes the merge resolve every field from live settings,
                    // which is the correct behaviour when there is no user JSON.
                    m_rawJsonProfiles.insert(*path, PhosphorAnimation::Profile{});
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

        // `global` is the root of EVERY path's chain (ProfilePaths::parentPath
        // answers "global" for every category root), so whichever layer this
        // entry lands in applies to the whole tree.
        //
        // Published untagged it sits in the resolver's upper layer and, being
        // fully engaged whether or not the user ever opened the page,
        // overwrites every field of all 29 family seeds on every path —
        // leaving the seed table inert and every surface animating with one
        // uniform feel. Published under the seed tag it sits alongside the
        // seeds at the chain root, where the deeper per-family seeds
        // correctly override it.
        //
        // So the tag follows user intent: while the global blob is unset the
        // shipped per-family character wins, and the moment the user actually
        // sets a global value it outranks the seeds again, which is what a
        // "retime everything" control has to mean.
        if (settingsExplicit) {
            reg.registerProfile(*path, settingsProfile);
        } else {
            reg.registerProfile(*path, settingsProfile, QString(kShellAnimationFamilySeedsOwnerTag));
        }
    }
}

} // namespace PlasmaZones
