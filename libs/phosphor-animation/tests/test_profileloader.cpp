// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace PhosphorAnimation;

class TestProfileLoader : public QObject
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
        PhosphorProfileRegistry::instance().clear();
    }

    /// Missing directory is a no-op.
    void testMissingDirectoryIsNoOp()
    {
        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        const int n = loader.loadFromDirectory(QStringLiteral("/no/such/dir"));
        QCOMPARE(n, 0);
        QCOMPARE(PhosphorProfileRegistry::instance().profileCount(), 0);
    }

    /// Valid profile JSON — path from name field, profile fields
    /// round-trip through Profile::fromJson.
    void testLoadValidProfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("overlay.json")), QStringLiteral(R"({
            "name": "overlay.fade",
            "curve": "spring:14.0,0.6",
            "duration": 250,
            "minDistance": 0
        })"));

        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 1);

        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 250.0);
        QVERIFY(resolved->curve);
    }

    /// Missing name field — skipped.
    void testMissingNameIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("nameless.json")), QStringLiteral(R"({
            "duration": 250
        })"));

        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// Malformed JSON — skipped.
    void testMalformedJsonIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("broken.json")), QStringLiteral("{\"name\": \"x\", broken"));

        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// User-overrides-system collision pattern (decision X). Loads the
    /// system dir first (lower priority), then user dir overrides on
    /// the same name. systemSourcePath tracked on the user entry for
    /// future restore.
    void testUserOverridesSystem()
    {
        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        writeFile(systemDir.filePath(QStringLiteral("p.json")), QStringLiteral(R"({
            "name": "x",
            "duration": 100
        })"));
        writeFile(userDir.filePath(QStringLiteral("p.json")), QStringLiteral(R"({
            "name": "x",
            "duration": 500
        })"));

        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("x"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 500.0);

        const auto entries = loader.entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().sourcePath, userDir.filePath(QStringLiteral("p.json")));
        QCOMPARE(entries.first().systemSourcePath, systemDir.filePath(QStringLiteral("p.json")));
    }

    /// `name` field becomes the registry path; it must NOT leak into
    /// the Profile's presetName (two distinct concepts).
    void testNameDoesNotLeakIntoPresetName()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("p.json")), QStringLiteral(R"({
            "name": "overlay.fade",
            "presetName": "My Overlay Preset",
            "duration": 150
        })"));

        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());
        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->presetName.value_or(QString()), QStringLiteral("My Overlay Preset"));
    }

    /// Rescan on a file that actually changes content fires the signal.
    /// Previously this test wrote `{"name": "a"}` with no other fields
    /// and only checked that the signal fired — which would pass on any
    /// broken parser that happily registers empty profiles. Now we
    /// write a semantically-meaningful profile and also assert the
    /// resolved value round-trips.
    void testRescanFiresSignal()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("p.json")), QStringLiteral(R"({
            "name": "a",
            "duration": 123,
            "minDistance": 7
        })"));
        ProfileLoader loader(PhosphorProfileRegistry::instance(), QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());

        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("a"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(-1.0), 123.0);
        QCOMPARE(resolved->minDistance.value_or(-1), 7);

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);
        loader.requestRescan();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 200);
    }

    /// Partitioned-ownership contract: a daemon-direct registration
    /// (empty owner) MUST survive a ProfileLoader rescan that doesn't
    /// mention the same path. Direct-wipe-on-rescan was the PR-344
    /// blocker that caused daemon fan-out profiles to be evicted.
    void testLoaderDoesNotWipeDaemonDirectEntries()
    {
        auto& reg = PhosphorProfileRegistry::instance();

        // Daemon publishes a "direct" (untagged) entry first.
        Profile daemonProfile;
        daemonProfile.duration = 42.0;
        reg.registerProfile(QStringLiteral("Global"), daemonProfile);
        QVERIFY(reg.hasProfile(QStringLiteral("Global")));
        QCOMPARE(reg.ownerOf(QStringLiteral("Global")), QString());

        // User drops ONE profile JSON for a different path.
        QTemporaryDir userDir;
        writeFile(userDir.filePath(QStringLiteral("zone.json")),
                  QStringLiteral(R"({"name": "Zone", "duration": 200})"));
        ProfileLoader loader(reg, QStringLiteral("test-user-profiles"));
        loader.loadFromDirectory(userDir.path());

        // Loader's rescan must NOT have touched the direct "Global" entry.
        QVERIFY(reg.hasProfile(QStringLiteral("Global")));
        auto resolvedDaemon = reg.resolve(QStringLiteral("Global"));
        QVERIFY(resolvedDaemon.has_value());
        QCOMPARE(resolvedDaemon->duration.value_or(-1.0), 42.0);

        // And loader-owned "Zone" is there under its owner tag.
        QCOMPARE(reg.ownerOf(QStringLiteral("Zone")), QStringLiteral("test-user-profiles"));
    }

    /// Delete-user-restores-system end-to-end test. The PR reviewers
    /// flagged that the documentation claimed this works but no test
    /// actually exercises it.
    void testDeleteUserRestoresSystem()
    {
        auto& reg = PhosphorProfileRegistry::instance();

        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        writeFile(systemDir.filePath(QStringLiteral("p.json")),
                  QStringLiteral(R"({"name": "shared", "duration": 100})"));
        writeFile(userDir.filePath(QStringLiteral("p.json")), QStringLiteral(R"({"name": "shared", "duration": 500})"));

        ProfileLoader loader(reg, QStringLiteral("test-restore"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        // User wins initially.
        auto resolved = reg.resolve(QStringLiteral("shared"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 500.0);

        // Delete user copy, request rescan.
        QVERIFY(QFile::remove(userDir.filePath(QStringLiteral("p.json"))));
        loader.requestRescan();
        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);
        QVERIFY(spy.wait(500));

        // System is restored via re-parse on the next rescan.
        resolved = reg.resolve(QStringLiteral("shared"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 100.0);
    }
};

QTEST_MAIN(TestProfileLoader)
#include "test_profileloader.moc"
