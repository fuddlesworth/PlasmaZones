// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animation_profile_publication.cpp
 * @brief Daemon-level integration: setupAnimationProfiles + publishActiveAnimationProfile
 *
 * Constructing a real `PlasmaZones::Daemon` requires D-Bus session bus,
 * KWin compositor binding, and a long stack of optional KDE singletons —
 * way out of scope for a unit test. Per the test plan: build a minimal
 * subset using the same `m_curveRegistry` / `m_curveLoader` /
 * `m_profileLoader` / `Settings` plumbing and exercise the contract.
 *
 * Three load-bearing invariants under test (all from `setupAnimationProfiles`
 * in src/daemon/daemon.cpp):
 *
 *  1. **Loader picks up a JSON file → registry has the user profile at
 *     the loader's tag.** A profile dropped under the user dir before
 *     the loader scans must end up in `PhosphorProfileRegistry` tagged
 *     with the loader's owner tag — proving the directory walk + sink
 *     commit + registry write all wire correctly.
 *
 *  2. **Settings change → registry's `Global` is direct-owned with the
 *     updated value.** A `Settings::setAnimationProfile` call must fan
 *     out to `PhosphorProfileRegistry::registerProfile(Global, ...)`
 *     under the empty/direct owner tag (NOT the loader's tag). This is
 *     the user-tunable slider path.
 *
 *  3. **Loader sees a `Global.json` collision → registry stays
 *     direct-owned (Settings wins via the Phase 1b inversion).** When
 *     a user drops a `Global.json` into the profile dir AFTER Settings
 *     has already published, the loader's commitBatch must NOT stomp
 *     the direct-owned entry. This is enforced inside
 *     `PhosphorProfileRegistry::reloadFromOwner` — a path owned by the
 *     direct/empty tag survives a partitioned reload from a different
 *     tag.
 */

#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorFsLoader/DirectoryLoader.h>

#include "../../../src/config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using namespace PhosphorAnimation;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

/// Owner tag string mirroring `kPlasmaZonesUserProfilesOwnerTag` in
/// src/daemon/daemon.cpp. Duplicated rather than included because the
/// daemon's `kPlasmaZonesUserProfilesOwnerTag` is a file-scope static
/// — copying it here keeps the test free of daemon-internal headers.
/// A future rename in the daemon would not break this test (the value
/// is opaque to the contract under test); the assertion is "loader
/// uses SOME non-empty tag and Settings uses the direct/empty tag",
/// not "the tag string is exactly this literal".
const QString kLoaderOwnerTag = QStringLiteral("plasmazones-user-profiles");

void writeFile(const QString& path, const QString& contents)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    QTextStream s(&f);
    s << contents;
}

} // namespace

class TestAnimationProfilePublication : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    void init()
    {
        // Each test starts with a clean process-wide profile registry —
        // production code uses `clearOwner(...)` + `unregisterProfile(...)`
        // for narrow cleanup; tests get the full sledgehammer.
        PhosphorProfileRegistry::instance().clear();
    }

    void cleanup()
    {
        PhosphorProfileRegistry::instance().clear();
    }

    /// Invariant 1: a profile JSON dropped under the user dir BEFORE
    /// the loader runs must land in PhosphorProfileRegistry tagged with
    /// the loader's owner tag.
    void testLoaderRegistersUserProfileUnderLoaderTag()
    {
        IsolatedConfigGuard guard;

        // Build the user profile dir layout the daemon would scan.
        const QString profileDir = guard.dataPath() + QStringLiteral("/plasmazones/profiles");
        QVERIFY(QDir().mkpath(profileDir));

        // Drop a profile JSON. Note: filename basename must match the
        // inner `name` field (ProfileLoader rejects mismatches).
        writeFile(profileDir + QStringLiteral("/zone.highlight.json"), QStringLiteral(R"({
            "name": "zone.highlight",
            "duration": 175,
            "curve": "0.42,0.00,0.58,1.00"
        })"));

        // Build the daemon's loader plumbing — same shapes, no daemon
        // construction required.
        CurveRegistry curveRegistry;
        ProfileLoader profileLoader(PhosphorProfileRegistry::instance(), curveRegistry, kLoaderOwnerTag);
        QCOMPARE(profileLoader.loadFromDirectory(profileDir), 1);

        // Registry now contains the user profile.
        auto& registry = PhosphorProfileRegistry::instance();
        QVERIFY(registry.hasProfile(QStringLiteral("zone.highlight")));

        // The owner tag is the loader's — partitioning means this
        // entry is subject to wholesale replacement by another
        // `reloadFromOwner(loaderTag, ...)` but invisible to direct
        // `registerProfile(path, profile)` calls (those land under the
        // empty/direct tag).
        QCOMPARE(registry.ownerOf(QStringLiteral("zone.highlight")), kLoaderOwnerTag);

        // The resolved Profile carries the values from the JSON file.
        const auto resolved = registry.resolve(QStringLiteral("zone.highlight"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->effectiveDuration(), 175.0);
    }

    /// Invariant 2: a Settings setter must fan out to
    /// `registerProfile(Global, ...)` under the direct/empty owner tag.
    /// This is the daemon's `publishActiveAnimationProfile` minimal
    /// shape — Settings is the slider surface; the registry's `Global`
    /// path is the canonical broadcast key.
    void testSettingsChangeFansOutToGlobalAsDirectOwner()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        auto& registry = PhosphorProfileRegistry::instance();

        // Mirror the daemon's `publishActiveAnimationProfile` body:
        // every settings-driven path is registered with `registerProfile`
        // (the two-arg overload — empty/direct owner). We wire that
        // explicitly here so the test is self-contained.
        auto publish = [&]() {
            const Profile p = settings.animationProfile();
            registry.registerProfile(ProfilePaths::Global, p);
        };

        // Initial publish — registry now carries Settings' default
        // animation profile under the direct/empty owner.
        publish();
        QVERIFY(registry.hasProfile(ProfilePaths::Global));
        QCOMPARE(registry.ownerOf(ProfilePaths::Global), QString());

        // Mutate settings and re-publish — the registry must reflect
        // the new value. Capture the registry's profileChanged signal
        // so we observe the per-path notification too.
        QSignalSpy spy(&registry, &PhosphorProfileRegistry::profileChanged);

        Profile updated = settings.animationProfile();
        updated.duration = (updated.effectiveDuration() == 250.0) ? 350.0 : 250.0;
        settings.setAnimationProfile(updated);
        publish();

        // Per-path signal fired AT LEAST once for the Global path.
        // (Settings::setAnimationProfile internally also drives some
        // signals; this assertion is on the registry-side fan-out.)
        bool sawGlobal = false;
        for (const auto& args : spy) {
            if (args.value(0).toString() == ProfilePaths::Global) {
                sawGlobal = true;
                break;
            }
        }
        QVERIFY2(sawGlobal, "PhosphorProfileRegistry did not emit profileChanged for Global after Settings update");

        // Direct ownership preserved across re-publication.
        QCOMPARE(registry.ownerOf(ProfilePaths::Global), QString());

        // Resolved Profile reflects the updated duration. Settings
        // round-trips through serialization (curve canonicalisation
        // etc.), so use effectiveDuration rather than raw == comparison.
        const auto resolved = registry.resolve(ProfilePaths::Global);
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->effectiveDuration(), updated.effectiveDuration());
    }

    /// Invariant 3 (Phase 1b inversion): a loader rescan that lands a
    /// `Global.json` file MUST NOT evict the direct-owned Settings
    /// entry. The contract is enforced inside
    /// `PhosphorProfileRegistry::reloadFromOwner` — paths owned by the
    /// direct/empty tag survive a partitioned reload from a different
    /// tag.
    ///
    /// Without this property, dropping a `Global.json` would silently
    /// override the user's Settings slider — a regression that's
    /// invisible until users reach for "why doesn't my slider do
    /// anything?".
    void testLoaderRespectsDirectOwnedGlobalCollision()
    {
        IsolatedConfigGuard guard;

        Settings settings;
        auto& registry = PhosphorProfileRegistry::instance();

        // Step 1 — Settings publishes Global as direct-owned.
        Profile settingsProfile = settings.animationProfile();
        settingsProfile.duration = 425.0; // distinctive value
        settings.setAnimationProfile(settingsProfile);
        registry.registerProfile(ProfilePaths::Global, settings.animationProfile());

        QCOMPARE(registry.ownerOf(ProfilePaths::Global), QString());
        const auto preLoaderResolved = registry.resolve(ProfilePaths::Global);
        QVERIFY(preLoaderResolved.has_value());
        QCOMPARE(preLoaderResolved->effectiveDuration(), 425.0);

        // Step 2 — drop a Global.json into the user profile dir, then
        // load via ProfileLoader with the partitioned owner tag. The
        // loader's commitBatch routes through reloadFromOwner, which
        // (per Phase 1b) silently skips paths already owned by the
        // direct/empty tag.
        const QString profileDir = guard.dataPath() + QStringLiteral("/plasmazones/profiles");
        QVERIFY(QDir().mkpath(profileDir));
        // Loader requires the basename and inner name to match.
        writeFile(profileDir + QStringLiteral("/global.json"), QStringLiteral(R"({
            "name": "global",
            "duration": 999
        })"));

        CurveRegistry curveRegistry;
        ProfileLoader profileLoader(PhosphorProfileRegistry::instance(), curveRegistry, kLoaderOwnerTag);
        const int loaded = profileLoader.loadFromDirectory(profileDir);
        QCOMPARE(loaded, 1);

        // The Global entry MUST still be direct-owned and carry the
        // Settings value (425), NOT the loader's value (999). If
        // `reloadFromOwner` lacked the direct-owner guard, the loader
        // would have stomped Settings here.
        QCOMPARE(registry.ownerOf(ProfilePaths::Global), QString());
        const auto postLoaderResolved = registry.resolve(ProfilePaths::Global);
        QVERIFY(postLoaderResolved.has_value());
        QCOMPARE(postLoaderResolved->effectiveDuration(), 425.0);
    }
};

QTEST_MAIN(TestAnimationProfilePublication)
#include "test_animation_profile_publication.moc"
