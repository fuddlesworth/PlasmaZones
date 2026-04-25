// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace PhosphorAnimation;

/**
 * @brief Cross-loader integration: CurveLoader + ProfileLoader against a
 *        shared CurveRegistry.
 *
 * The Daemon wires both loaders against the same `CurveRegistry`
 * instance and connects `CurveLoader::curvesChanged →
 * ProfileLoader::requestRescan` so a profile JSON whose `curve` field
 * names a user-authored curve resolves to the registered curve once
 * both loaders have run. This test pins that integration: a profile
 * referencing a user curve (via the curve's registered name) must
 * resolve to a non-null `Profile::curve` whose typeId / wire-format
 * matches the registered curve.
 *
 * Without the cross-wire (or with the wires reversed), a profile
 * loaded BEFORE the curve is registered captures a null
 * `shared_ptr<const Curve>` and silently falls back to the library
 * default — a regression that's invisible until a user visually
 * notices "my custom curve isn't taking effect".
 */
class TestLoaderIntegration : public QObject
{
    Q_OBJECT

private:
    void writeFile(const QString& path, const QString& contents) const
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QTextStream s(&f);
        s << contents;
    }

private Q_SLOTS:

    void init()
    {
        // Each test gets a clean process-wide profile registry. Tests
        // are the sanctioned caller per the registry's class doc.
        PhosphorProfileRegistry::instance().clear();
    }

    /// Curves loaded BEFORE profiles — the first profile parse already
    /// resolves the curve. This is the happy path: with curves on disk
    /// before profiles, `Profile::fromJson` calls `CurveRegistry::create`
    /// inside `ProfileLoader::Sink::parseFile` and the resulting
    /// `Profile::curve` is non-null.
    void testProfileResolvesUserCurve_curvesBeforeProfiles()
    {
        QTemporaryDir curveDir;
        QTemporaryDir profileDir;
        QVERIFY(curveDir.isValid() && profileDir.isValid());

        // User-authored curve: a cubic-bezier with non-default control
        // points so it's distinguishable from the library default.
        writeFile(curveDir.filePath(QStringLiteral("my-curve.json")), QStringLiteral(R"({
            "name": "my-curve",
            "displayName": "My Bezier",
            "typeId": "cubic-bezier",
            "parameters": { "x1": 0.25, "y1": 0.75, "x2": 0.75, "y2": 0.25 }
        })"));

        // Profile that references the curve by name. ProfileLoader
        // requires the filename basename to match the inner `name`
        // field, so the file is `my.profile.json` and the registry path
        // is `my.profile`.
        writeFile(profileDir.filePath(QStringLiteral("my.profile.json")), QStringLiteral(R"({
            "name": "my.profile",
            "curve": "my-curve",
            "duration": 250
        })"));

        // SHARED registry — both loaders write/read here. This is the
        // contract the daemon's setupAnimationProfiles depends on.
        CurveRegistry curveRegistry;
        auto& profileRegistry = PhosphorProfileRegistry::instance();

        CurveLoader curveLoader(curveRegistry);
        ProfileLoader profileLoader(profileRegistry, curveRegistry);

        // Curves first so the profile-parse already sees the registered
        // curve. This is the daemon's documented load order.
        QCOMPARE(curveLoader.loadFromDirectory(curveDir.path()), 1);
        QVERIFY(curveRegistry.has(QStringLiteral("my-curve")));

        QCOMPARE(profileLoader.loadFromDirectory(profileDir.path()), 1);
        QVERIFY(profileRegistry.hasProfile(QStringLiteral("my.profile")));

        // Resolve through the registry — the path "my.profile" must
        // produce a profile with a non-null curve whose typeId matches
        // the registered curve.
        const auto resolved = profileRegistry.resolve(QStringLiteral("my.profile"));
        QVERIFY(resolved.has_value());
        QVERIFY2(resolved->curve != nullptr,
                 "profile.curve is null — the curve registered in CurveRegistry was not resolved by Profile::fromJson");
        // typeId for cubic-bezier curves is "bezier" (the canonical
        // typeId — the JSON typeId field accepts "cubic-bezier" as an
        // alias on parse, but the runtime Curve emits "bezier").
        QCOMPARE(resolved->curve->typeId(), QStringLiteral("bezier"));

        // The resolved curve must round-trip to the same wire format as
        // a registry-direct lookup — this pins that we got OUR curve,
        // not a library default that happens to have the same typeId.
        const auto registryDirect = curveRegistry.create(QStringLiteral("my-curve"));
        QVERIFY(registryDirect);
        QCOMPARE(resolved->curve->toString(), registryDirect->toString());
    }

    /// Curves loaded AFTER profiles — the cross-wire that the daemon
    /// installs (curveLoader.curvesChanged → profileLoader.requestRescan)
    /// re-parses every profile so the freshly-registered curve is
    /// resolved. Mirrors the daemon wiring in setupAnimationProfiles.
    ///
    /// Without the wire, a profile parsed before the curve was
    /// registered would carry `Profile::curve == nullptr` for the rest
    /// of the process lifetime (until the profile JSON itself was
    /// touched).
    void testProfileResolvesUserCurve_profilesBeforeCurves()
    {
        QTemporaryDir curveDir;
        QTemporaryDir profileDir;
        QVERIFY(curveDir.isValid() && profileDir.isValid());

        writeFile(curveDir.filePath(QStringLiteral("late-curve.json")), QStringLiteral(R"({
            "name": "late-curve",
            "typeId": "cubic-bezier",
            "parameters": { "x1": 0.42, "y1": 0.0, "x2": 0.58, "y2": 1.0 }
        })"));

        writeFile(profileDir.filePath(QStringLiteral("late.profile.json")), QStringLiteral(R"({
            "name": "late.profile",
            "curve": "late-curve",
            "duration": 300
        })"));

        CurveRegistry curveRegistry;
        auto& profileRegistry = PhosphorProfileRegistry::instance();

        CurveLoader curveLoader(curveRegistry);
        ProfileLoader profileLoader(profileRegistry, curveRegistry);

        // Mirror the daemon's wire: a curve-rescan triggers a profile
        // rescan so newly-available curves get resolved into existing
        // profiles. ProfileLoader::requestRescan goes through the
        // debounced rescan path.
        connect(&curveLoader, &CurveLoader::curvesChanged, &profileLoader, &ProfileLoader::requestRescan);

        // Profiles first — the curve is missing, so the parse stores
        // `Profile::curve = nullptr` (per Profile::fromJson contract:
        // an unresolved curve spec leaves the field null and falls
        // back to the library default).
        QCOMPARE(profileLoader.loadFromDirectory(profileDir.path()), 1);
        {
            const auto resolved = profileRegistry.resolve(QStringLiteral("late.profile"));
            QVERIFY(resolved.has_value());
            // Initial parse: curve unresolved (null); duration set.
            QVERIFY2(resolved->curve == nullptr,
                     "test precondition: curve must be unresolved on initial parse (curve not yet registered)");
            QCOMPARE(resolved->duration.value_or(0.0), 300.0);
        }

        // Now load the curve. The cross-wire fires: curvesChanged →
        // profileLoader rescan. Rescan re-parses every profile file —
        // the curve is now registered, so the second pass installs it.
        QSignalSpy reloadSpy(&profileLoader, &ProfileLoader::profilesChanged);
        QCOMPARE(curveLoader.loadFromDirectory(curveDir.path()), 1);

        // ProfileLoader's rescan is debounced (~50 ms default). The
        // queued rescan fires profilesChanged once the second commit
        // produces a value-diff.
        QVERIFY2(reloadSpy.wait(2000),
                 "ProfileLoader did not re-emit profilesChanged after curvesChanged → requestRescan wire");

        // After the cross-wire fires, the resolved profile carries the
        // newly-registered curve.
        const auto resolved = profileRegistry.resolve(QStringLiteral("late.profile"));
        QVERIFY(resolved.has_value());
        QVERIFY2(resolved->curve != nullptr,
                 "profile.curve is still null after curvesChanged → requestRescan — cross-wire is broken");
        // typeId for cubic-bezier curves is the canonical "bezier" string.
        QCOMPARE(resolved->curve->typeId(), QStringLiteral("bezier"));
    }
};

QTEST_MAIN(TestLoaderIntegration)
#include "test_loader_integration.moc"
