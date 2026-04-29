// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveRegistry.h>
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
    CurveRegistry m_curveRegistry;
    /// Per-test-fixture profile registry. Phase A3 of the architecture
    /// refactor retired `m_profileRegistry` —
    /// composition roots and tests own their own registries. Each test
    /// method gets a freshly cleared registry via init() / cleanup().
    PhosphorProfileRegistry m_profileRegistry;
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
        m_profileRegistry.clear();
    }

    /// Guarantees no registry state leaks between test methods even
    /// when a test forgets to clean up after itself. `init()` already
    /// clears on entry, but pairing with `cleanup()` keeps the
    /// guarantee end-to-end rather than entry-only.
    void cleanup()
    {
        m_profileRegistry.clear();
    }

    /// Missing directory is a no-op.
    void testMissingDirectoryIsNoOp()
    {
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        const int n = loader.loadFromDirectory(QStringLiteral("/no/such/dir"));
        QCOMPARE(n, 0);
        QCOMPARE(m_profileRegistry.profileCount(), 0);
    }

    /// Valid profile JSON — path from name field, profile fields
    /// round-trip through Profile::fromJson.
    void testLoadValidProfile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("overlay.fade.json")), QStringLiteral(R"({
            "name": "overlay.fade",
            "curve": "spring:14.0,0.6",
            "duration": 250,
            "minDistance": 0
        })"));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 1);

        auto resolved = m_profileRegistry.resolve(QStringLiteral("overlay.fade"));
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

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// Malformed JSON — skipped.
    void testMalformedJsonIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("broken.json")), QStringLiteral("{\"name\": \"x\", broken"));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
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
        writeFile(systemDir.filePath(QStringLiteral("x.json")), QStringLiteral(R"({
            "name": "x",
            "duration": 100
        })"));
        writeFile(userDir.filePath(QStringLiteral("x.json")), QStringLiteral(R"({
            "name": "x",
            "duration": 500
        })"));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        auto resolved = m_profileRegistry.resolve(QStringLiteral("x"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 500.0);

        const auto entries = loader.entries();
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().sourcePath, userDir.filePath(QStringLiteral("x.json")));
        QCOMPARE(entries.first().systemSourcePath, systemDir.filePath(QStringLiteral("x.json")));
    }

    /// `name` field becomes the registry path; it must NOT leak into
    /// the Profile's presetName (two distinct concepts).
    void testNameDoesNotLeakIntoPresetName()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("overlay.fade.json")), QStringLiteral(R"({
            "name": "overlay.fade",
            "presetName": "My Overlay Preset",
            "duration": 150
        })"));

        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());
        auto resolved = m_profileRegistry.resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->presetName.value_or(QString()), QStringLiteral("My Overlay Preset"));
    }

    /// `profilesChanged` fires on a rescan that sees content change on
    /// disk. Asserts the post-rescan resolved value matches the new file
    /// so a broken parser can't pass the spy assertion by registering
    /// empty profiles.
    void testRescanFiresSignal_whenContentChanged()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a.json"));
        writeFile(path, QStringLiteral(R"({
            "name": "a",
            "duration": 123,
            "minDistance": 7
        })"));
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());

        auto resolved = m_profileRegistry.resolve(QStringLiteral("a"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(-1.0), 123.0);
        QCOMPARE(resolved->minDistance.value_or(-1), 7);

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);

        // Mutate the file on disk — next rescan sees new values.
        QVERIFY(QFile::remove(path));
        writeFile(path, QStringLiteral(R"({
            "name": "a",
            "duration": 456,
            "minDistance": 9
        })"));

        loader.requestRescan();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);

        // Round-trip sanity: the registry reflects the new values.
        auto reResolved = m_profileRegistry.resolve(QStringLiteral("a"));
        QVERIFY(reResolved.has_value());
        QCOMPARE(reResolved->duration.value_or(-1.0), 456.0);
        QCOMPARE(reResolved->minDistance.value_or(-1), 9);
    }

    /// `profilesChanged` must NOT fire on a no-op rescan — the
    /// commitBatch diff in ProfileLoader::Sink suppresses the consumer
    /// signal when the re-parsed profile set is identical to the
    /// previous commit.
    void testRescanDoesNotFireSignal_whenContentUnchanged()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("stable.json")), QStringLiteral(R"({
            "name": "stable",
            "duration": 200,
            "minDistance": 5
        })"));
        ProfileLoader loader(m_profileRegistry, m_curveRegistry, QStringLiteral("test"));
        loader.loadFromDirectory(dir.path());

        // Attach spy AFTER initial load — only interested in the
        // rescan-produced signal (or its absence).
        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);

        loader.requestRescan();
        QVERIFY2(!spy.wait(300),
                 "profilesChanged fired on a no-op rescan — commitBatch diff did not suppress unchanged content");
        QCOMPARE(spy.count(), 0);
    }

    /// Partitioned-ownership contract: a daemon-direct registration
    /// (empty owner) MUST survive a ProfileLoader rescan that doesn't
    /// mention the same path. Direct-wipe-on-rescan was the PR-344
    /// blocker that caused daemon fan-out profiles to be evicted.
    void testLoaderDoesNotWipeDaemonDirectEntries()
    {
        auto& reg = m_profileRegistry;

        // Daemon publishes a "direct" (untagged) entry first.
        Profile daemonProfile;
        daemonProfile.duration = 42.0;
        reg.registerProfile(QStringLiteral("Global"), daemonProfile);
        QVERIFY(reg.hasProfile(QStringLiteral("Global")));
        QCOMPARE(reg.ownerOf(QStringLiteral("Global")), QString());

        // User drops ONE profile JSON for a different path.
        QTemporaryDir userDir;
        writeFile(userDir.filePath(QStringLiteral("Zone.json")),
                  QStringLiteral(R"({"name": "Zone", "duration": 200})"));
        ProfileLoader loader(reg, m_curveRegistry, QStringLiteral("test-user-profiles"));
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
        auto& reg = m_profileRegistry;

        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        writeFile(systemDir.filePath(QStringLiteral("shared.json")),
                  QStringLiteral(R"({"name": "shared", "duration": 100})"));
        writeFile(userDir.filePath(QStringLiteral("shared.json")),
                  QStringLiteral(R"({"name": "shared", "duration": 500})"));

        ProfileLoader loader(reg, m_curveRegistry, QStringLiteral("test-restore"));
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

        // User wins initially.
        auto resolved = reg.resolve(QStringLiteral("shared"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->duration.value_or(0.0), 500.0);

        // Delete user copy, request rescan.
        QVERIFY(QFile::remove(userDir.filePath(QStringLiteral("shared.json"))));
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
