// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationbootstrap.h"

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/ProfileLoader.h>

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
} // namespace

AnimationBootstrap::AnimationBootstrap()
    : m_curveRegistry(std::make_unique<PhosphorAnimation::CurveRegistry>())
{
    using namespace PhosphorAnimation;

    // Mirror Daemon::setupAnimationProfiles directory discovery —
    // `QStandardPaths::locateAll` walks XDG_DATA_DIRS in priority order
    // (writable user FIRST), and the loader iterates left-to-right with
    // last-write-wins semantics. Reverse so system dirs load first and
    // the user-writable dir overrides last (decision X).
    QStringList curveDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/curves"), QStandardPaths::LocateDirectory);
    std::reverse(curveDirs.begin(), curveDirs.end());
    QStringList profileDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/profiles"), QStandardPaths::LocateDirectory);
    std::reverse(profileDirs.begin(), profileDirs.end());

    // Always include the user-writable dir so the loader's
    // QFileSystemWatcher can see new drops without restart, even on a
    // fresh install where the directory doesn't exist yet.
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

    m_curveLoader = std::make_unique<CurveLoader>(*m_curveRegistry, nullptr);
    m_profileLoader =
        std::make_unique<ProfileLoader>(m_profileRegistry, *m_curveRegistry, kSecondaryProfilesOwnerTag, nullptr);

    // Curves first so any profile JSON referencing a user-authored curve
    // resolves on first parse — same ordering rationale as Daemon::
    // setupAnimationProfiles.
    m_curveLoader->loadLibraryBuiltins();
    m_curveLoader->loadFromDirectories(curveDirs, LiveReload::On);

    m_profileLoader->loadLibraryBuiltins();
    m_profileLoader->loadFromDirectories(profileDirs, LiveReload::On);
}

AnimationBootstrap::~AnimationBootstrap() = default;

} // namespace PlasmaZones
