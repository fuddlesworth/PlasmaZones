// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Spring.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace PhosphorAnimation;

class TestCurveLoader : public QObject
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

    // Unregister curves the test registered so subsequent tests don't
    // see them via the process-wide CurveRegistry singleton.
    void cleanupRegistry(const QStringList& names) const
    {
        for (const QString& name : names) {
            CurveRegistry::instance().unregisterFactory(name);
        }
    }

private Q_SLOTS:
    /// Missing directory is a no-op, not a failure.
    void testMissingDirectoryIsNoOp()
    {
        CurveLoader loader;
        const int n = loader.loadFromDirectory(QStringLiteral("/does/not/exist"), CurveRegistry::instance());
        QCOMPARE(n, 0);
        QCOMPARE(loader.registeredCount(), 0);
    }

    /// Valid spring JSON — registers under its name, resolves through
    /// CurveRegistry::create.
    void testLoadValidSpringCurve()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        writeFile(dir.filePath(QStringLiteral("my-spring.json")), QStringLiteral(R"({
            "name": "my-spring",
            "displayName": "My Spring",
            "typeId": "spring",
            "parameters": { "omega": 14.0, "zeta": 0.6 }
        })"));

        CurveLoader loader;
        const int n = loader.loadFromDirectory(dir.path(), CurveRegistry::instance());
        QCOMPARE(n, 1);

        auto resolved = CurveRegistry::instance().create(QStringLiteral("my-spring"));
        QVERIFY(resolved);
        QCOMPARE(resolved->typeId(), QStringLiteral("spring"));

        cleanupRegistry({QStringLiteral("my-spring")});
    }

    /// Missing name field — skipped with a warning, loader count stays 0.
    void testMissingNameIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("nameless.json")), QStringLiteral(R"({
            "typeId": "spring",
            "parameters": { "omega": 14.0, "zeta": 0.6 }
        })"));

        CurveLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), CurveRegistry::instance()), 0);
    }

    /// Unknown typeId — skipped.
    void testUnknownTypeIdIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("weird.json")), QStringLiteral(R"({
            "name": "bad",
            "typeId": "warp-drive",
            "parameters": {}
        })"));

        CurveLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), CurveRegistry::instance()), 0);
    }

    /// Malformed JSON — skipped.
    void testMalformedJsonIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("broken.json")), QStringLiteral("{\"name\": \"x\", typeId"));

        CurveLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), CurveRegistry::instance()), 0);
    }

    /// Two directories — later wins on name collision. systemSourcePath
    /// preserved on the user entry so restore is possible.
    void testLoadFromDirectoriesUserWins()
    {
        QTemporaryDir systemDir;
        QTemporaryDir userDir;
        QVERIFY(systemDir.isValid() && userDir.isValid());

        writeFile(systemDir.filePath(QStringLiteral("shared.json")), QStringLiteral(R"({
            "name": "shared",
            "typeId": "spring",
            "parameters": { "omega": 10.0, "zeta": 0.9 }
        })"));
        writeFile(userDir.filePath(QStringLiteral("shared.json")), QStringLiteral(R"({
            "name": "shared",
            "typeId": "spring",
            "parameters": { "omega": 20.0, "zeta": 0.3 }
        })"));

        CurveLoader loader;
        loader.loadFromDirectories({systemDir.path(), userDir.path()}, CurveRegistry::instance());

        const auto entries = loader.entries();
        QCOMPARE(entries.size(), 1);
        // User entry wins, system path preserved for restore.
        QCOMPARE(entries.first().sourcePath, userDir.filePath(QStringLiteral("shared.json")));
        QCOMPARE(entries.first().systemSourcePath, systemDir.filePath(QStringLiteral("shared.json")));

        cleanupRegistry({QStringLiteral("shared")});
    }

    /// Cubic-bezier parameters round-trip.
    void testCubicBezierCurve()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("bez.json")), QStringLiteral(R"({
            "name": "user-bezier",
            "typeId": "cubic-bezier",
            "parameters": { "x1": 0.25, "y1": 0.75, "x2": 0.75, "y2": 0.25 }
        })"));

        CurveLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), CurveRegistry::instance()), 1);
        auto c = CurveRegistry::instance().create(QStringLiteral("user-bezier"));
        QVERIFY(c);
        cleanupRegistry({QStringLiteral("user-bezier")});
    }

    /// Elastic curve parameters round-trip.
    void testElasticCurve()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("e.json")), QStringLiteral(R"({
            "name": "user-elastic",
            "typeId": "elastic-out",
            "parameters": { "amplitude": 1.5, "period": 0.4 }
        })"));

        CurveLoader loader;
        QCOMPARE(loader.loadFromDirectory(dir.path(), CurveRegistry::instance()), 1);
        cleanupRegistry({QStringLiteral("user-elastic")});
    }

    /// requestRescan + signal fires after rescanAll runs (via debounce
    /// timer). Verified by starting rescan and processing events.
    void testRescanFiresSignal()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("initial.json")), QStringLiteral(R"({
            "name": "initial",
            "typeId": "spring",
            "parameters": { "omega": 14.0, "zeta": 0.6 }
        })"));

        CurveLoader loader;
        loader.loadFromDirectory(dir.path(), CurveRegistry::instance());

        QSignalSpy spy(&loader, &CurveLoader::curvesChanged);
        loader.requestRescan();
        // Wait up to 200 ms for the 50ms-debounced timer to fire.
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 200);
        cleanupRegistry({QStringLiteral("initial")});
    }
};

QTEST_MAIN(TestCurveLoader)
#include "test_curveloader.moc"
