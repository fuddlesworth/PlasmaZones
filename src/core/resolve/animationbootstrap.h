// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QLatin1StringView>
#include <QStringList>
#include <QVariantMap>

#include <memory>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace PhosphorAnimation {
class CurveLoader;
class CurveRegistry;
} // namespace PhosphorAnimation

namespace PlasmaZones {

/// Owner-tag partition used by `seedShellAnimationFamilies`. Exposed
/// so daemon teardown / reconfigure paths can `clearOwner(tag)` to
/// wipe just the family-seed partition without touching settings-driven
/// or user-JSON entries.
extern PLASMAZONES_EXPORT const QLatin1StringView kShellAnimationFamilySeedsOwnerTag;

/// XDG-discovered curve and profile directories — `plasmazones/curves`
/// and `plasmazones/profiles` resolved against `XDG_DATA_DIRS` (lowest-
/// priority first), with the user-writable dir appended last, giving
/// `sys-lowest, ..., sys-highest, user`. The scan reverse-iterates that and
/// applies first-registration-wins, so the user dir claims its keys
/// first. The user dirs are materialised on disk so live-reload watchers
/// attach via `WatchedDirectorySet`'s parent-watch climb on fresh installs.
///
/// Returned alongside the loader pair from
/// `constructAnimationLoaders` so callers that wire additional signals
/// before the initial scan can pass the same lists into
/// `runInitialCurveLoad`.
///
/// Curves only. There was a `profileDirs` beside this, kept on the stated
/// grounds that the saved-preset library still needed it — it did not: the
/// animations page derives that path itself, and nothing ever read the field.
/// Per-event timing overrides have not lived in a directory since schema v8;
/// they are config, under `Animations/MotionProfileTree`.
struct AnimationLoaderDirs
{
    QStringList curveDirs;
};

/// Pair of caller-owned loaders — composition roots store these as
/// members so the QFileSystemWatcher inside each survives for the
/// process lifetime (or until explicit teardown).
struct AnimationLoaderHandles
{
    std::unique_ptr<PhosphorAnimation::CurveLoader> curveLoader;
    AnimationLoaderDirs dirs;
};

/// Discover XDG `plasmazones/{curves,profiles}` directories, materialise
/// the user-writable dirs, and construct the CurveLoader bound to
/// @p curveRegistry.
///
/// Does NOT call `loadLibraryBuiltins` / `loadFromDirectories` — callers
/// run those AFTER they have wired any consumer-side signals so the
/// initial scan's emits are observed. Drive them with
/// `runInitialCurveLoad`, then `seedShellAnimationFamilies`, then
/// `installMotionProfileTree` with the config-backed timing tree.
///
/// The returned `unique_ptr` is caller-owned. `parent` is forwarded to the
/// loader's `QObject` parent (use `nullptr` when the caller stores it via
/// `unique_ptr` and wants no Qt parent ownership).
PLASMAZONES_EXPORT AnimationLoaderHandles constructAnimationLoaders(PhosphorAnimation::CurveRegistry& curveRegistry,
                                                                    QObject* parent = nullptr);

/// Curve half of the initial load. Always run this FIRST, before
/// `seedShellAnimationFamilies` and `installMotionProfileTree`, so both of
/// those resolve a named curve on their first parse instead of storing a null
/// curve and falling back to the library default. Internally calls
/// `loadLibraryBuiltins` then `loadFromDirectories` with LiveReload::On.
PLASMAZONES_EXPORT void runInitialCurveLoad(PhosphorAnimation::CurveLoader& curveLoader,
                                            const AnimationLoaderDirs& dirs);

/// Install the per-event TIMING overrides carried by @p treeJson into
/// @p registry, under @p ownerTag, replacing whatever that tag held before.
///
/// @p treeJson is `ISettings::motionProfileTree()` — the serialized
/// `PhosphorAnimation::ProfileTree` stored at `Animations/MotionProfileTree`.
/// This is the timing counterpart of the decoration tree, and it replaced the
/// loose `plasmazones/profiles/<event.path>.json` files in schema v8: an
/// animation event's pack and its timing now live in one store, so a settings
/// profile captures both and a motion set writes both the way a decoration set
/// always wrote a whole surface.
///
/// The tree's BASELINE is deliberately ignored. The global profile is its own
/// setting (`Settings::animationProfile`), which each composition root already
/// registers at `ProfilePaths::Global`, and importing a second copy here would
/// double-register the same level under a different owner. Nothing writes a
/// baseline into this key.
///
/// MUST run AFTER `runInitialCurveLoad` (so a profile naming a user-authored
/// curve resolves on first parse) and AFTER `seedShellAnimationFamilies` (so a
/// user override at a seeded path wins). Re-run it whenever the setting
/// changes, and whenever the curve registry reloads — a Profile holds its
/// resolved curve, so a curve edit needs the tree re-parsed against the fresh
/// registry.
PLASMAZONES_EXPORT void installMotionProfileTree(PhosphorAnimation::PhosphorProfileRegistry& registry,
                                                 const PhosphorAnimation::CurveRegistry& curves,
                                                 const QVariantMap& treeJson, const QString& ownerTag);

/// Register the shell's family-level Profile defaults (the parent
/// paths every QML profile binding eventually walks up to) so an
/// unconfigured leaf inherits a sensible curve/duration shape rather
/// than the library default of 150 ms OutCubic. Reproduces the
/// per-family character of the prior bundled per-leaf JSONs (popups
/// feel different from windows feel different from OSDs) without
/// reintroducing the per-leaf shadowing problem
/// that motivated their deletion: every entry is registered under the
/// `kShellAnimationFamilySeedsOwnerTag` partition, which the registry treats
/// as its low-precedence layer, so a per-event override from
/// `Animations/MotionProfileTree` at any leaf or at the family parent itself
/// silently wins.
///
/// MUST be called AFTER curves are loaded (so curve names like
/// `widget-out` resolve via `CurveRegistry::tryCreate`) and BEFORE
/// `installMotionProfileTree` (so a config override at a seeded path lands in
/// the upper layer above the seed rather than racing it).
///
/// MUST also be re-run whenever the curve registry reloads, for the same
/// reason the timing tree is re-installed then: each seeded Profile holds the
/// curve it RESOLVED at parse time, so a curve edited on disk leaves every
/// seed on the pre-edit object until they are built again.
PLASMAZONES_EXPORT void seedShellAnimationFamilies(PhosphorAnimation::PhosphorProfileRegistry& registry,
                                                   const PhosphorAnimation::CurveRegistry& curves);

/// Owns the per-process CurveRegistry, PhosphorProfileRegistry, and the
/// loaders that populate them from shipped + user JSONs. The
/// composition root that constructs an AnimationBootstrap is
/// responsible for publishing the registries via their respective
/// `setDefaultRegistry` calls (`PhosphorCurve::setDefaultRegistry`,
/// `PhosphorProfileRegistry::setDefaultRegistry`,
/// `QtQuickClockManager::setDefaultManager`) — those publications live
/// in the composition-root code (editor's main.cpp, settings's main.cpp,
/// or the daemon's setupAnimationProfiles) rather than here, because
/// the QML-side handles (`PhosphorCurve` / `QtQuickClockManager`) live
/// in the QML module which `plasmazones_core` does not link against.
///
/// Each composition root constructs one of these in `main()` before
/// loading QML and keeps it alive for the application lifetime. Use
/// the `profileRegistry()` / `curveRegistry()` accessors to thread the
/// owned registries into other services or to publish them.
///
/// The daemon owns equivalent wiring directly in `Daemon` (where the
/// registries are full-fat members alongside the rest of the daemon's
/// services); `AnimationBootstrap` is the lightweight shape for
/// processes that don't need the rest of the daemon machinery.
///
/// This is the PlasmaZones-flavoured wrapper — it scans
/// `${XDG_DATA_DIRS}/plasmazones/{curves,profiles}` and the user-writable
/// equivalents, mirroring the daemon. Library-level loaders stay
/// consumer-agnostic per Phase-4 decision U.
///
/// Note the trade this makes: the ctor runs the full three-step load itself,
/// so there is no seam between loader construction and the initial scan. A
/// consumer that has to OBSERVE that first scan (`curvesChanged` /
/// `profilesChanged` fire synchronously from registration, through the
/// underlying DirectoryLoader) must use `constructAnimationLoaders` plus
/// the `runInitial*Load` helpers directly, as the daemon does. Neither the
/// settings app nor the editor needs to, which is why they get the one-liner.
class PLASMAZONES_EXPORT AnimationBootstrap
{
public:
    AnimationBootstrap();
    ~AnimationBootstrap();

    AnimationBootstrap(const AnimationBootstrap&) = delete;
    AnimationBootstrap& operator=(const AnimationBootstrap&) = delete;

    /// Borrowed accessors for callers that need to thread the same
    /// registry into other services in the composition root, or to
    /// publish the registry pointers to QML via the static-default
    /// handles. The `AnimationBootstrap` instance must outlive any
    /// borrow.
    PhosphorAnimation::PhosphorProfileRegistry* profileRegistry()
    {
        return &m_profileRegistry;
    }
    PhosphorAnimation::CurveRegistry* curveRegistry()
    {
        return m_curveRegistry.get();
    }
    /// Install the config-backed per-event timing tree
    /// (`ISettings::motionProfileTree()`) into this bootstrap's registry.
    ///
    /// The ctor cannot do this itself: it runs before the composition root has
    /// a Settings instance, and the tree changes at runtime. Call it once the
    /// settings object exists, and again on every `motionProfileTreeChanged`.
    void applyMotionProfileTree(const QVariantMap& treeJson);

    /// Register the global animation Profile (`Settings::animationProfile()`)
    /// at `ProfilePaths::Global`, the root of every chain.
    ///
    /// Without this a secondary process resolves every event from the family
    /// seeds alone while the daemon resolves it against the user's global
    /// values, so the settings app previews one timing and the compositor
    /// plays another.
    ///
    /// @p explicitlySet is `Settings::hasExplicitAnimationProfile()`, and it
    /// selects the LAYER, matching the daemon. An unset global is a shipped
    /// default and belongs beneath the per-family seeds; a global the user
    /// actually chose is an instruction to retime everything and belongs
    /// above them. Passing the wrong value here silently makes the preview
    /// disagree with the compositor, which is the whole reason the method
    /// exists.
    void applyGlobalProfile(const PhosphorAnimation::Profile& profile, bool explicitlySet);

private:
    std::unique_ptr<PhosphorAnimation::CurveRegistry> m_curveRegistry;
    PhosphorAnimation::PhosphorProfileRegistry m_profileRegistry;
    std::unique_ptr<PhosphorAnimation::CurveLoader> m_curveLoader;
};

} // namespace PlasmaZones
