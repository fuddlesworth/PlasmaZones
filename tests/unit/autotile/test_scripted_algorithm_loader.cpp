// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include <QTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QSignalSpy>

#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmLoader.h>

#include "../helpers/AutotileTestHelpers.h"
#include "../helpers/ScriptTestHelpers.h"
#include "../helpers/XdgEnvGuard.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Minimal valid script content with a given name
 */
static QString validScript(const QString& name)
{
    return QStringLiteral(
               "var metadata = { name: \"%1\", description: \"Test algorithm\" };\n"
               "function calculateZones(params) {\n"
               "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
               "}\n")
        .arg(name);
}

class TestScriptedAlgorithmLoader : public QObject
{
    Q_OBJECT

private:
    /**
     * @brief Unregister any script:* algorithms left over from a test
     */
    void cleanupScriptedAlgorithms(const QStringList& ids)
    {
        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        for (const QString& id : ids) {
            if (registry->hasAlgorithm(id)) {
                registry->unregisterAlgorithm(id);
            }
        }
    }

private Q_SLOTS:

    void cleanupTestCase()
    {
        cleanupScriptedAlgorithms({QStringLiteral("script:gamma"), QStringLiteral("script:valid-name"),
                                   QStringLiteral("script:shared"), QStringLiteral("script:ephemeral")});
    }

    /**
     * @brief Per-test cleanup — prevent registry pollution between tests
     *
     * Qt Test calls cleanup() after each test function. We unregister all
     * script:* algorithm IDs that any test might have registered so that
     * one test's registrations never leak into the next.
     */
    void cleanup()
    {
        cleanupScriptedAlgorithms({QStringLiteral("script:gamma"), QStringLiteral("script:valid-name"),
                                   QStringLiteral("script:shared"), QStringLiteral("script:ephemeral"),
                                   QStringLiteral("script:has space"), QStringLiteral("script:has.dot"),
                                   QStringLiteral("script:special!char"), QStringLiteral("script:priority"),
                                   QStringLiteral("script:user_only")});
    }

    // =========================================================================
    // scanAndRegister tests
    // =========================================================================

    void testScanAndRegister_registersValidScripts()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QDir().mkpath(algoDir);

        writeScript(algoDir, QStringLiteral("gamma.js"), validScript(QStringLiteral("Gamma")));

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());

        QSignalSpy spy(&*loader, &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged);
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:gamma")));
        QCOMPARE(spy.count(), 1);

        // Note: both XDG_DATA_DIRS and XDG_DATA_HOME point to the same directory in this
        // test, so the loader treats it as a user script. The testUserOverridesSystem test
        // below verifies isUserScript() with separate system/user directories.

        loader.reset();
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
    }

    // =========================================================================
    // Filename validation tests
    // =========================================================================

    void testFilenameValidation_invalidCharsRejected()
    {
        XdgEnvGuard envGuard;

        // The loader validates filenames against ^[a-zA-Z0-9_-]+$
        // Files with spaces, dots (in basename), or other special chars are rejected
        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QDir().mkpath(algoDir);

        // Valid filename
        writeScript(algoDir, QStringLiteral("valid-name.js"), validScript(QStringLiteral("Valid")));
        // Invalid: spaces
        writeScript(algoDir, QStringLiteral("has space.js"), validScript(QStringLiteral("HasSpace")));
        // Invalid: dots in basename
        writeScript(algoDir, QStringLiteral("has.dot.js"), validScript(QStringLiteral("HasDot")));
        // Invalid: special chars
        writeScript(algoDir, QStringLiteral("special!char.js"), validScript(QStringLiteral("Special")));

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:valid-name")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:has space")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:has.dot")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:special!char")));

        loader.reset();
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
    }

    // =========================================================================
    // User overrides system priority
    // =========================================================================

    void testUserOverridesSystem()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        // System dir
        QString systemAlgoDir = xdgRoot.path() + QStringLiteral("/system/plasmazones/algorithms");
        QDir().mkpath(systemAlgoDir);
        writeScript(systemAlgoDir, QStringLiteral("shared.js"), validScript(QStringLiteral("SystemVersion")));

        // User dir
        QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QDir().mkpath(userAlgoDir);
        writeScript(userAlgoDir, QStringLiteral("shared.js"), validScript(QStringLiteral("UserVersion")));

        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user")).toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:shared")));

        // The user version should have overridden the system version
        auto* algo = registry->algorithm(QStringLiteral("script:shared"));
        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("UserVersion"));
        QVERIFY(algo->isUserScript()); // user version should be marked as user script

        loader.reset();
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
    }

    // =========================================================================
    // Stale script cleanup on rescan
    // =========================================================================

    void testStaleScriptCleanup()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QDir().mkpath(algoDir);

        QString scriptPath =
            writeScript(algoDir, QStringLiteral("ephemeral.js"), validScript(QStringLiteral("Ephemeral")));
        QVERIFY(!scriptPath.isEmpty());

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:ephemeral")));

        // Delete the script file
        QVERIFY(QFile::remove(scriptPath));

        // Rescan — the stale algorithm should be unregistered
        loader->scanAndRegister();
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:ephemeral")));

        loader.reset();
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
    }

    /// Regression guard: passing `LiveReload::Off` disables the
    /// background watcher and gives the caller a one-shot scan.
    /// Verifies the new optional parameter on `scanAndRegister`
    /// (a) compiles and (b) doesn't regress the basic scan path.
    /// Tests that opt out of live-reload do so to avoid spurious
    /// directoryChanged signals fighting with QSignalSpy timing.
    void testScanAndRegister_liveReloadOff()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        const QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(algoDir));

        writeScript(algoDir, QStringLiteral("ephemeral.js"), validScript(QStringLiteral("Ephemeral")));

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.scanAndRegister(PhosphorFsLoader::LiveReload::Off);

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:ephemeral")));
    }

    // =========================================================================
    // System-vs-system priority — the highest-priority XDG_DATA_DIRS entry
    // must win when two system dirs ship the same script id. Pre-fix, the
    // reverse-iteration in performScan combined with skip-on-collision in
    // loadFromDirectory made the LOWEST-priority system dir win — silently
    // shipping a stale algorithm whenever a distro provided one and the
    // local /usr/local/share/... package shipped a newer version.
    // =========================================================================

    void testSystemVsSystem_highestPriorityWins()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        // Two system dirs — sysHigh is listed FIRST in XDG_DATA_DIRS
        // so it should win for collisions.
        const QString sysHighDir = xdgRoot.path() + QStringLiteral("/sys-high/plasmazones/algorithms");
        const QString sysLowDir = xdgRoot.path() + QStringLiteral("/sys-low/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(sysHighDir));
        QVERIFY(QDir().mkpath(sysLowDir));

        // Same id, distinguishable name fields — the winner's name field
        // tells us which dir's script registered.
        writeScript(sysHighDir, QStringLiteral("priority.js"), validScript(QStringLiteral("HighPriority")));
        writeScript(sysLowDir, QStringLiteral("priority.js"), validScript(QStringLiteral("LowPriority")));

        // sys-high listed first => higher priority per XDG spec.
        const QByteArray xdgDirs = (xdgRoot.path() + QStringLiteral("/sys-high")).toUtf8() + ":"
            + (xdgRoot.path() + QStringLiteral("/sys-low")).toUtf8();
        qputenv("XDG_DATA_DIRS", xdgDirs);
        // Point user dir at a separate clean location so it doesn't interfere.
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user-home")).toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:priority")));

        auto* algo = registry->algorithm(QStringLiteral("script:priority"));
        QVERIFY(algo != nullptr);
        QCOMPARE(algo->name(), QStringLiteral("HighPriority"));
        QVERIFY(!algo->isUserScript());

        loader.reset();
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
    }

    /// Three-tier check: user > sys-high > sys-low. Validates the
    /// full XDG semantic in one shot.
    void testThreeTierPriority_userBeatsSysHighBeatsSysLow()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        const QString userHomeDir = xdgRoot.path() + QStringLiteral("/user-home/plasmazones/algorithms");
        const QString sysHighDir = xdgRoot.path() + QStringLiteral("/sys-high/plasmazones/algorithms");
        const QString sysLowDir = xdgRoot.path() + QStringLiteral("/sys-low/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(userHomeDir));
        QVERIFY(QDir().mkpath(sysHighDir));
        QVERIFY(QDir().mkpath(sysLowDir));

        // Shared id `priority` exists in all three — user wins overall.
        writeScript(userHomeDir, QStringLiteral("priority.js"), validScript(QStringLiteral("UserVersion")));
        writeScript(sysHighDir, QStringLiteral("priority.js"), validScript(QStringLiteral("HighSystem")));
        writeScript(sysLowDir, QStringLiteral("priority.js"), validScript(QStringLiteral("LowSystem")));

        // `user_only` lives only in user dir; should be present.
        writeScript(userHomeDir, QStringLiteral("user_only.js"), validScript(QStringLiteral("UserOnly")));

        const QByteArray xdgDirs = (xdgRoot.path() + QStringLiteral("/sys-high")).toUtf8() + ":"
            + (xdgRoot.path() + QStringLiteral("/sys-low")).toUtf8();
        qputenv("XDG_DATA_DIRS", xdgDirs);
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user-home")).toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();

        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:priority")));
        auto* shared = registry->algorithm(QStringLiteral("script:priority"));
        QVERIFY(shared != nullptr);
        QCOMPARE(shared->name(), QStringLiteral("UserVersion"));
        QVERIFY(shared->isUserScript());

        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:user_only")));

        loader.reset();
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
    }

    // =========================================================================
    // ensureUserDirectoryExists
    // =========================================================================

    void testEnsureUserDirectoryExists()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        QString expectedDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");

        // Make sure it doesn't exist yet
        QVERIFY(!QDir(expectedDir).exists());

        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.ensureUserDirectoryExists();

        // The directory should now exist
        QVERIFY(QDir(expectedDir).exists());
    }
};

QTEST_MAIN(TestScriptedAlgorithmLoader)
#include "test_scripted_algorithm_loader.moc"
