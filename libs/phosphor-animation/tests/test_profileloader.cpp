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
        ProfileLoader loader;
        const int n = loader.loadFromDirectory(QStringLiteral("/no/such/dir"), PhosphorProfileRegistry::instance());
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

        ProfileLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), PhosphorProfileRegistry::instance()), 1);

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

        ProfileLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), PhosphorProfileRegistry::instance()), 0);
    }

    /// Malformed JSON — skipped.
    void testMalformedJsonIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("broken.json")), QStringLiteral("{\"name\": \"x\", broken"));

        ProfileLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), PhosphorProfileRegistry::instance()), 0);
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

        ProfileLoader loader;
        loader.loadFromDirectories({systemDir.path(), userDir.path()}, PhosphorProfileRegistry::instance());

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

        ProfileLoader loader;
        loader.loadFromDirectory(dir.path(), PhosphorProfileRegistry::instance());
        auto resolved = PhosphorProfileRegistry::instance().resolve(QStringLiteral("overlay.fade"));
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->presetName.value_or(QString()), QStringLiteral("My Overlay Preset"));
    }

    void testRescanFiresSignal()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("p.json")), QStringLiteral(R"({"name": "a"})"));
        ProfileLoader loader;
        loader.loadFromDirectory(dir.path(), PhosphorProfileRegistry::instance());

        QSignalSpy spy(&loader, &ProfileLoader::profilesChanged);
        loader.requestRescan();
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 200);
    }
};

QTEST_MAIN(TestProfileLoader)
#include "test_profileloader.moc"
