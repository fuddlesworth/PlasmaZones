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
    CurveRegistry m_registry;

    void writeFile(const QString& path, const QString& contents) const
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        QTextStream s(&f);
        s << contents;
    }

    // Unregister curves the test registered so subsequent tests don't
    // leak into later test methods.
    void cleanupRegistry(const QStringList& names)
    {
        for (const QString& name : names) {
            m_registry.unregisterFactory(name);
        }
    }

private Q_SLOTS:
    /// Missing directory is a no-op, not a failure.
    void testMissingDirectoryIsNoOp()
    {
        CurveLoader loader(m_registry);
        const int n = loader.loadFromDirectory(QStringLiteral("/does/not/exist"));
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

        CurveLoader loader(m_registry);
        const int n = loader.loadFromDirectory(dir.path());
        QCOMPARE(n, 1);

        auto resolved = m_registry.create(QStringLiteral("my-spring"));
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

        CurveLoader loader(m_registry);
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// Unknown typeId — skipped.
    void testUnknownTypeIdIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("bad.json")), QStringLiteral(R"({
            "name": "bad",
            "typeId": "warp-drive",
            "parameters": {}
        })"));

        CurveLoader loader(m_registry);
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
    }

    /// Malformed JSON — skipped.
    void testMalformedJsonIsRejected()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("broken.json")), QStringLiteral("{\"name\": \"x\", typeId"));

        CurveLoader loader(m_registry);
        QCOMPARE(loader.loadFromDirectory(dir.path()), 0);
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

        CurveLoader loader(m_registry);
        loader.loadFromDirectories({systemDir.path(), userDir.path()});

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
        writeFile(dir.filePath(QStringLiteral("user-bezier.json")), QStringLiteral(R"({
            "name": "user-bezier",
            "typeId": "cubic-bezier",
            "parameters": { "x1": 0.25, "y1": 0.75, "x2": 0.75, "y2": 0.25 }
        })"));

        CurveLoader loader(m_registry);
        QCOMPARE(loader.loadFromDirectory(dir.path()), 1);
        auto c = m_registry.create(QStringLiteral("user-bezier"));
        QVERIFY(c);
        cleanupRegistry({QStringLiteral("user-bezier")});
    }

    /// Elastic curve parameters round-trip.
    void testElasticCurve()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("user-elastic.json")), QStringLiteral(R"({
            "name": "user-elastic",
            "typeId": "elastic-out",
            "parameters": { "amplitude": 1.5, "period": 0.4 }
        })"));

        CurveLoader loader(m_registry);
        QCOMPARE(loader.loadFromDirectory(dir.path()), 1);
        cleanupRegistry({QStringLiteral("user-elastic")});
    }

    /// `curvesChanged` fires on a rescan that sees new content on disk.
    /// Exercises the positive-emit side of the commitBatch diff — the
    /// old shape of this test called `requestRescan()` with no content
    /// change and passed trivially on a loader that emitted
    /// unconditionally.
    void testRescanFiresSignal_whenContentChanged()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("initial.json"));
        writeFile(path, QStringLiteral(R"({
            "name": "initial",
            "typeId": "spring",
            "parameters": { "omega": 14.0, "zeta": 0.6 }
        })"));

        CurveLoader loader(m_registry);
        loader.loadFromDirectory(dir.path());

        QSignalSpy spy(&loader, &CurveLoader::curvesChanged);

        // Mutate the underlying file so the next rescan sees new wire-
        // format for the same key. QFile::remove + writeFile gets a
        // clean atomic rewrite and avoids partial-read races.
        QVERIFY(QFile::remove(path));
        writeFile(path, QStringLiteral(R"({
            "name": "initial",
            "typeId": "spring",
            "parameters": { "omega": 20.0, "zeta": 0.3 }
        })"));

        loader.requestRescan();
        // 50 ms debounce + scheduling slack — 500 ms is more than enough
        // on a loaded machine without being flaky on a cold one.
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 500);
        cleanupRegistry({QStringLiteral("initial")});
    }

    /// `curvesChanged` must NOT fire on a rescan that re-parses the same
    /// files without any content difference — the commitBatch diff in
    /// CurveLoader::Sink catches this and suppresses the consumer signal.
    ///
    /// Without this guard, a cross-process D-Bus rescan trigger or a
    /// harmless `touch(1)`-style mtime bump would churn every bound
    /// consumer's rebind logic on no-op rescans.
    void testRescanDoesNotFireSignal_whenContentUnchanged()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("stable.json")), QStringLiteral(R"({
            "name": "stable",
            "typeId": "spring",
            "parameters": { "omega": 14.0, "zeta": 0.6 }
        })"));

        CurveLoader loader(m_registry);
        loader.loadFromDirectory(dir.path());

        // Attach the spy AFTER the initial load — only interested in
        // the rescan-produced signal (or lack thereof).
        QSignalSpy spy(&loader, &CurveLoader::curvesChanged);

        loader.requestRescan();
        // Wait past the 50 ms debounce window and give the watcher a
        // chance to (not) fire. A clean no-op rescan should produce
        // zero signals within 300 ms.
        QVERIFY2(!spy.wait(300),
                 "curvesChanged fired on a no-op rescan — commitBatch diff did not suppress unchanged content");
        QCOMPARE(spy.count(), 0);
        cleanupRegistry({QStringLiteral("stable")});
    }

    /// Regression guard for the factory-leak-on-destruction bug: when a
    /// CurveLoader goes out of scope, every factory it registered into
    /// the borrowed CurveRegistry MUST be unregistered. Without this,
    /// tests that construct multiple sequential loaders (or a plugin
    /// host that tears down and rebuilds a loader) would accumulate
    /// ghost factories capturing stale `shared_ptr<const Curve>`
    /// instances — live but no longer tracked by any loader, and
    /// impossible to evict without a manual sweep.
    ///
    /// This mirrors `ProfileLoader::~ProfileLoader`'s `clearOwner(tag)`
    /// on PhosphorProfileRegistry, adapted to CurveRegistry's flat
    /// typeId→Factory shape.
    void testDestructorUnregistersFactories()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("ephemeral.json")), QStringLiteral(R"({
            "name": "ephemeral",
            "typeId": "spring",
            "parameters": { "omega": 10.0, "zeta": 0.7 }
        })"));

        QVERIFY2(!m_registry.has(QStringLiteral("ephemeral")),
                 "test precondition: registry must not already know the ephemeral key");

        {
            CurveLoader loader(m_registry);
            QCOMPARE(loader.loadFromDirectory(dir.path()), 1);
            QVERIFY(m_registry.has(QStringLiteral("ephemeral")));
        } // loader goes out of scope here

        QVERIFY2(!m_registry.has(QStringLiteral("ephemeral")),
                 "CurveLoader dtor must unregister factories it installed — see M1 fix");
    }
};

QTEST_MAIN(TestCurveLoader)
#include "test_curveloader.moc"
