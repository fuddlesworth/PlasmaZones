// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationbootstrap.h"

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/ProfileLoader.h>

#include <QDir>
#include <QObject>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>

namespace PlasmaZones {

namespace {
// Owner-tag partition used by secondary processes' ProfileLoader. Distinct
// from the daemon's tag so the registry remains correctly partitioned even
// in a hypothetical scenario where daemon and settings/editor share a
// process — today they don't, but the narrower contract is correct.
constexpr QLatin1StringView kSecondaryProfilesOwnerTag{"plasmazones-secondary-profiles"};

QStringList discoverDataDirs(QLatin1StringView xdgRelative)
{
    // `QStandardPaths::locateAll` returns directories in priority order —
    // the writable user location FIRST, system dirs AFTER. The loader
    // iterates in caller-supplied order and lets later entries override
    // earlier on key collision, so we reverse to achieve the standard
    // "system first, user wins last" layering (decision X). Matches
    // LayoutManager::loadLayouts.
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QString(xdgRelative),
                                                 QStandardPaths::LocateDirectory);
    std::reverse(dirs.begin(), dirs.end());

    // locateAll returns an empty list when none of the candidate dirs
    // exist yet (fresh install). Unconditionally include the writable
    // location so the loader can watch it via the parent-directory
    // fallback — once the user drops a file there, the watcher fires
    // and the loader picks it up without a daemon restart.
    const QString userDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + QString(xdgRelative);
    if (!dirs.contains(userDir)) {
        dirs.append(userDir);
    }
    return dirs;
}

QString writableUserDir(QLatin1StringView xdgRelative)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + QString(xdgRelative);
}
} // namespace

AnimationLoaderHandles constructAnimationLoaders(PhosphorAnimation::CurveRegistry& curveRegistry,
                                                 PhosphorAnimation::PhosphorProfileRegistry& profileRegistry,
                                                 QLatin1StringView ownerTag, QObject* parent)
{
    using namespace PhosphorAnimation;

    AnimationLoaderHandles handles;
    handles.dirs.curveDirs = discoverDataDirs(QLatin1StringView{"plasmazones/curves"});
    handles.dirs.profileDirs = discoverDataDirs(QLatin1StringView{"plasmazones/profiles"});

    // Materialise the user dirs eagerly so live-reload works on fresh
    // installs. `WatchedDirectorySet`'s parent-watch climb refuses to
    // attach a `QFileSystemWatcher` to forbidden ancestors (`$HOME`,
    // `$XDG_DATA_HOME`, etc.) — without these dirs existing, the climb
    // terminates at `~/.local/share` and NO watch is installed. The
    // user could then drop `~/.local/share/plasmazones/profiles/foo.json`
    // and live-reload would silently never fire until daemon restart.
    // Failures are non-fatal — the initial on-demand scan still works
    // without a watch.
    QDir().mkpath(writableUserDir(QLatin1StringView{"plasmazones/curves"}));
    QDir().mkpath(writableUserDir(QLatin1StringView{"plasmazones/profiles"}));

    // Construct loaders with NO initial load — callers run the scan
    // explicitly (via runInitialAnimationLoad) AFTER they have wired
    // any consumer-side signals so the initial scan's emits are
    // observed. The daemon needs this; secondary processes call
    // runInitialAnimationLoad immediately and have no pre-load wiring
    // to set up.
    //
    // Registry reference is captured at loader construction — this
    // prevents any later async rescan from landing on a different
    // registry than the one the caller initialized against.
    handles.curveLoader = std::make_unique<CurveLoader>(curveRegistry, parent);
    handles.profileLoader = std::make_unique<ProfileLoader>(profileRegistry, curveRegistry, ownerTag, parent);

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
    QObject::connect(handles.curveLoader.get(), &CurveLoader::curvesChanged, handles.profileLoader.get(),
                     &ProfileLoader::requestRescan);

    return handles;
}

void runInitialAnimationLoad(PhosphorAnimation::CurveLoader& curveLoader,
                             PhosphorAnimation::ProfileLoader& profileLoader, const AnimationLoaderDirs& dirs)
{
    using namespace PhosphorAnimation;

    // Curves first so any profile JSON referencing a user-authored curve
    // resolves on first parse rather than waiting for the curveLoader→
    // profileLoader rescan wire to fire on the second pass.
    //
    // Library-level pack first (today a no-op — the library ships no
    // bundled curves/profiles — but kept for future curve-pack additions).
    curveLoader.loadLibraryBuiltins();
    curveLoader.loadFromDirectories(dirs.curveDirs, LiveReload::On);

    profileLoader.loadLibraryBuiltins();
    profileLoader.loadFromDirectories(dirs.profileDirs, LiveReload::On);
}

AnimationBootstrap::AnimationBootstrap()
    : m_curveRegistry(std::make_unique<PhosphorAnimation::CurveRegistry>())
{
    auto handles =
        constructAnimationLoaders(*m_curveRegistry, m_profileRegistry, kSecondaryProfilesOwnerTag, /*parent=*/nullptr);
    m_curveLoader = std::move(handles.curveLoader);
    m_profileLoader = std::move(handles.profileLoader);

    runInitialAnimationLoad(*m_curveLoader, *m_profileLoader, handles.dirs);
}

AnimationBootstrap::~AnimationBootstrap() = default;

} // namespace PlasmaZones
