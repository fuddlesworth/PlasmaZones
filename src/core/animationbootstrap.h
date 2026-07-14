// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QLatin1StringView>
#include <QStringList>

#include <memory>

QT_BEGIN_NAMESPACE
class QObject;
QT_END_NAMESPACE

namespace PhosphorAnimation {
class CurveLoader;
class CurveRegistry;
class ProfileLoader;
} // namespace PhosphorAnimation

namespace PlasmaZones {

/// Owner-tag partition used by `seedShellAnimationFamilies`. Exposed
/// so daemon teardown / reconfigure paths can `clearOwner(tag)` to
/// wipe just the family-seed partition without touching settings-driven
/// or user-JSON entries.
extern PLASMAZONES_EXPORT const QLatin1StringView kShellAnimationFamilySeedsOwnerTag;

/// XDG-discovered curve and profile directories — `plasmazones/curves`
/// and `plasmazones/profiles` resolved against `XDG_DATA_DIRS` (lowest-
/// priority first), with the user-writable dir appended last so
/// last-writer-wins layering produces `sys-lowest, ..., sys-highest,
/// user`. The user dirs are materialised on disk so live-reload
/// watchers attach via `WatchedDirectorySet`'s parent-watch climb on
/// fresh installs.
///
/// Returned alongside the loader pair from
/// `constructAnimationLoaders` so callers that wire additional signals
/// before the initial scan can pass the same lists into
/// `runInitialCurveLoad` / `runInitialProfileLoad`.
struct AnimationLoaderDirs
{
    QStringList curveDirs;
    QStringList profileDirs;
};

/// Pair of caller-owned loaders — composition roots store these as
/// members so the QFileSystemWatcher inside each survives for the
/// process lifetime (or until explicit teardown).
struct AnimationLoaderHandles
{
    std::unique_ptr<PhosphorAnimation::CurveLoader> curveLoader;
    std::unique_ptr<PhosphorAnimation::ProfileLoader> profileLoader;
    AnimationLoaderDirs dirs;
};

/// Discover XDG `plasmazones/{curves,profiles}` directories, materialise
/// the user-writable dirs, and construct CurveLoader + ProfileLoader
/// bound to the supplied registries. The curveLoader's `curvesChanged`
/// is wired into the profileLoader's `requestRescan` so a curve JSON
/// edit triggers profile re-parsing (without this, profiles whose
/// `curve:` reference was unresolved at first parse stay unresolved
/// until the profile file itself is touched).
///
/// Does NOT call `loadLibraryBuiltins` / `loadFromDirectories` — callers
/// run those AFTER they have wired any consumer-side signals so the
/// initial scan's emits are observed by every listener. Drive them with
/// `runInitialCurveLoad`, then `seedShellAnimationFamilies`, then
/// `runInitialProfileLoad`.
///
/// The returned `unique_ptr`s are caller-owned. `ownerTag` partitions
/// the registry so a `clearOwner(tag)` teardown / rescan only touches
/// entries this loader registered. `parent` is forwarded to the
/// loaders' `QObject` parent (use `nullptr` when the caller stores the
/// loaders via `unique_ptr` and wants no Qt parent ownership).
PLASMAZONES_EXPORT AnimationLoaderHandles constructAnimationLoaders(
    PhosphorAnimation::CurveRegistry& curveRegistry, PhosphorAnimation::PhosphorProfileRegistry& profileRegistry,
    QLatin1StringView ownerTag, QObject* parent = nullptr);

/// Curve half of the initial load. Always run this BEFORE
/// `runInitialProfileLoad` so a profile JSON referencing a user-authored
/// curve resolves on first parse rather than waiting for the
/// curveLoader→profileLoader rescan wire to fire on the second pass.
/// Split from the profile half because every composition root has to
/// interleave `seedShellAnimationFamilies` between the two — the seeds
/// resolve named curves, and the profile loader's `reloadFromOwner` has
/// to be able to overwrite a seed the user authored a JSON for.
/// Internally calls `loadLibraryBuiltins` then `loadFromDirectories`
/// with LiveReload::On.
PLASMAZONES_EXPORT void runInitialCurveLoad(PhosphorAnimation::CurveLoader& curveLoader,
                                            const AnimationLoaderDirs& dirs);

/// Profile half of the initial load — see `runInitialCurveLoad` for
/// the rationale on splitting.
PLASMAZONES_EXPORT void runInitialProfileLoad(PhosphorAnimation::ProfileLoader& profileLoader,
                                              const AnimationLoaderDirs& dirs);

/// Register the shell's family-level Profile defaults (the parent
/// paths every QML profile binding eventually walks up to) so an
/// unconfigured leaf inherits a sensible curve/duration shape rather
/// than the library default of 150 ms OutCubic. Reproduces the
/// per-family character of the prior bundled per-leaf JSONs (popups
/// feel different from windows feel different from OSDs) without
/// reintroducing the per-leaf shadowing problem
/// that motivated their deletion: every entry is registered under the
/// `kShellAnimationFamilySeedsOwnerTag` partition, so a Settings-UI
/// edit (direct-owner) or a user-authored JSON (loader-tagged owner)
/// at any leaf or at the family parent itself silently wins.
///
/// MUST be called AFTER curves are loaded (so curve names like
/// `widget-out` resolve via `CurveRegistry::tryCreate`) and BEFORE
/// the profile loader's initial scan (so a user JSON at a seeded
/// path can correctly overwrite the seed).
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

private:
    std::unique_ptr<PhosphorAnimation::CurveRegistry> m_curveRegistry;
    PhosphorAnimation::PhosphorProfileRegistry m_profileRegistry;
    std::unique_ptr<PhosphorAnimation::CurveLoader> m_curveLoader;
    std::unique_ptr<PhosphorAnimation::ProfileLoader> m_profileLoader;
};

} // namespace PlasmaZones
