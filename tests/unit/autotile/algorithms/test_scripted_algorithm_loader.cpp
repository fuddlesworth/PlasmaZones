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

#include "helpers/AutotileTestHelpers.h"
#include "helpers/ScriptTestHelpers.h"
#include "helpers/XdgEnvGuard.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Minimal valid script content with a given name
 */
static QString validScript(const QString& name)
{
    return QStringLiteral(
               "local pluau = pluau\n"
               "return pluau.algorithm {\n"
               "    metadata = { name = \"%1\", description = \"Test algorithm\" },\n"
               "    tile = function(ctx)\n"
               "        return { { x = 0, y = 0, width = 100, height = 100 } }\n"
               "    end,\n"
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
    /// Every scripted id any slot in this file can register. Built once and used
    /// by BOTH cleanup() and cleanupTestCase(): they were two hand-kept lists
    /// and had already diverged, which left the spray ids live in the
    /// process-shared registry across slots.
    QStringList allRegisteredIds() const
    {
        QStringList ids{QStringLiteral("script:gamma"),    QStringLiteral("script:valid-name"),
                        QStringLiteral("script:shared"),   QStringLiteral("script:ephemeral"),
                        QStringLiteral("script:mutable"),  QStringLiteral("script:has space"),
                        QStringLiteral("script:has.dot"),  QStringLiteral("script:special!char"),
                        QStringLiteral("script:priority"), QStringLiteral("script:user_only"),
                        QStringLiteral("script:notile"),   QStringLiteral("script:wellformed")};
        // The per-directory cap slot writes cap+N numbered scripts. Covered by
        // range rather than by name so a change to the fixture size cannot
        // silently leave ids behind; the bound derives from the same constant
        // the spray slot uses (kCap + kExtra there).
        for (int i = 0; i < PhosphorTiles::ScriptedAlgorithmLoader::MaxWatchedFilesPerDir + 10; ++i)
            ids.append(QStringLiteral("script:spray-%1").arg(i, 3, 10, QLatin1Char('0')));
        return ids;
    }

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
        cleanupScriptedAlgorithms(allRegisteredIds());
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
        cleanupScriptedAlgorithms(allRegisteredIds());
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

        QVERIFY(!writeScript(algoDir, QStringLiteral("gamma.luau"), validScript(QStringLiteral("Gamma"))).isEmpty());

        // XDG_DATA_DIRS pinned alongside XDG_DATA_HOME, like every sibling
        // slot: XdgEnvGuard saves and restores rather than clearing, so an
        // ambient value would let the scan below pick up the machine's
        // installed /usr/share/plasmazones/algorithms and register every
        // bundled id into the process-shared test registry, past a cleanup()
        // that only knows a hardcoded list.
        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());

        QSignalSpy spy(&*loader, &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged);
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:gamma")));
        QCOMPARE(spy.count(), 1);

        // Note: XDG_DATA_DIRS points at an (empty) /system subdirectory while
        // XDG_DATA_HOME is the root the script was written under, so the
        // loader treats it as a user script. The testUserOverridesSystem test
        // below verifies isUserScript() with populated system AND user dirs.
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
        QVERIFY(
            !writeScript(algoDir, QStringLiteral("valid-name.luau"), validScript(QStringLiteral("Valid"))).isEmpty());
        // Invalid: spaces
        QVERIFY(
            !writeScript(algoDir, QStringLiteral("has space.luau"), validScript(QStringLiteral("HasSpace"))).isEmpty());
        // Invalid: dots in basename
        QVERIFY(!writeScript(algoDir, QStringLiteral("has.dot.luau"), validScript(QStringLiteral("HasDot"))).isEmpty());
        // Invalid: special chars
        QVERIFY(!writeScript(algoDir, QStringLiteral("special!char.luau"), validScript(QStringLiteral("Special")))
                     .isEmpty());

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
    }

    // =========================================================================
    // Content validation — a filename-valid script that is structurally invalid
    // (loads but exposes no tile() function) must NOT register, while a sibling
    // well-formed script in the same dir still does.
    // =========================================================================

    void testContentValidation_missingTileRejected()
    {
        XdgEnvGuard envGuard;

        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());
        const QString algoDir = xdgRoot.path() + QStringLiteral("/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(algoDir));

        // Valid filename, valid Luau, returns a table — but no tile() function.
        QVERIFY(!writeScript(algoDir, QStringLiteral("notile.luau"),
                             QStringLiteral("return { metadata = { name = \"No Tile\" } }\n"))
                     .isEmpty());
        // A well-formed sibling proves the scan ran and only the broken one was dropped.
        QVERIFY(!writeScript(algoDir, QStringLiteral("wellformed.luau"), validScript(QStringLiteral("Wellformed")))
                     .isEmpty());

        qputenv("XDG_DATA_DIRS", xdgRoot.path().toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        std::optional<PhosphorTiles::ScriptedAlgorithmLoader> loader;
        loader.emplace(QStringLiteral("plasmazones/algorithms"), PlasmaZones::TestHelpers::testRegistry());
        loader->scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:wellformed")));
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:notile")));
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
        QVERIFY(!writeScript(systemAlgoDir, QStringLiteral("shared.luau"), validScript(QStringLiteral("SystemVersion")))
                     .isEmpty());

        // User dir
        QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QDir().mkpath(userAlgoDir);
        QVERIFY(!writeScript(userAlgoDir, QStringLiteral("shared.luau"), validScript(QStringLiteral("UserVersion")))
                     .isEmpty());

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
    }

    // =========================================================================
    // Unchanged-file reuse across rescans
    // =========================================================================

    /// A file whose (size, mtime) still match the previous scan, and whose id
    /// still holds a live scripted registry entry, keeps that entry instead of
    /// being re-parsed into a fresh sandboxed Luau VM.
    ///
    /// Asserted on the entry POINTER rather than on the name: rebuilding
    /// produces an algorithm that compares equal on every value the registry
    /// exposes, so only identity can tell "kept" from "rebuilt and replaced".
    void testRescanKeepsTheEntryForAnUnchangedScript()
    {
        XdgEnvGuard envGuard;
        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        const QString systemAlgoDir = xdgRoot.path() + QStringLiteral("/system/plasmazones/algorithms");
        const QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(systemAlgoDir));
        QVERIFY(QDir().mkpath(userAlgoDir));
        QVERIFY(!writeScript(userAlgoDir, QStringLiteral("shared.luau"), validScript(QStringLiteral("Original")))
                     .isEmpty());
        // XDG_DATA_DIRS pinned to an empty tmp tree, like every sibling slot.
        // XdgEnvGuard saves and restores but does not clear, so leaving it
        // ambient lets the scan pick up the machine's installed
        // /usr/share/plasmazones/algorithms and register every bundled id into
        // the process-shared test registry, past a cleanup() that only knows a
        // hardcoded id list.
        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user")).toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        const PhosphorTiles::TilingAlgorithm* first = registry->algorithm(QStringLiteral("script:shared"));
        QVERIFY(first != nullptr);

        // Nothing touched on disk between the two scans.
        loader.scanAndRegister();

        QCOMPARE(registry->algorithm(QStringLiteral("script:shared")), first);
    }

    /// The shadowing case in both directions, which is where the reuse stamp
    /// could resurrect a file that should have been rebuilt.
    ///
    /// A user script appearing over a stamped bundled one must win, and the
    /// bundled file must NOT be stamped while it is shadowed — otherwise, when
    /// the user script goes away again, the bundled file would be "reused" into
    /// a registry entry that no longer belongs to it.
    void testShadowedScriptIsRebuiltRatherThanResurrected()
    {
        XdgEnvGuard envGuard;
        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        const QString systemAlgoDir = xdgRoot.path() + QStringLiteral("/system/plasmazones/algorithms");
        const QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(systemAlgoDir));
        QVERIFY(QDir().mkpath(userAlgoDir));
        QVERIFY(!writeScript(systemAlgoDir, QStringLiteral("shared.luau"), validScript(QStringLiteral("SystemVersion")))
                     .isEmpty());
        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user")).toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        QCOMPARE(registry->algorithm(QStringLiteral("script:shared"))->name(), QStringLiteral("SystemVersion"));

        // A user script arrives and claims the same id. It must win, even
        // though the bundled file is unchanged and carries a stamp.
        QVERIFY(!writeScript(userAlgoDir, QStringLiteral("shared.luau"), validScript(QStringLiteral("UserVersion")))
                     .isEmpty());
        loader.scanAndRegister();
        QCOMPARE(registry->algorithm(QStringLiteral("script:shared"))->name(), QStringLiteral("UserVersion"));

        // The user script goes away. The bundled file has not changed on disk
        // since its very first scan, but it was REFUSED (and so unstamped)
        // while shadowed, so it has to be rebuilt rather than reused into the
        // entry the user script left behind.
        QVERIFY(QFile::remove(userAlgoDir + QStringLiteral("/shared.luau")));
        loader.scanAndRegister();

        const PhosphorTiles::TilingAlgorithm* restored = registry->algorithm(QStringLiteral("script:shared"));
        QVERIFY(restored != nullptr);
        QCOMPARE(restored->name(), QStringLiteral("SystemVersion"));
        QVERIFY(!restored->isUserScript());
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

        const QString scriptPath =
            writeScript(algoDir, QStringLiteral("ephemeral.luau"), validScript(QStringLiteral("Ephemeral")));
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

        // Rescan — the stale algorithm should be unregistered AND the change must
        // be announced via algorithmsChanged so the editor/daemon/settings refresh
        // (the removal is invisible to consumers without that signal).
        QSignalSpy removalSpy(&*loader, &PhosphorTiles::ScriptedAlgorithmLoader::algorithmsChanged);
        loader->scanAndRegister();
        QVERIFY(!registry->hasAlgorithm(QStringLiteral("script:ephemeral")));
        QCOMPARE(removalSpy.count(), 1);
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

        QVERIFY(!writeScript(algoDir, QStringLiteral("ephemeral.luau"), validScript(QStringLiteral("Ephemeral")))
                     .isEmpty());

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
        QVERIFY(!writeScript(sysHighDir, QStringLiteral("priority.luau"), validScript(QStringLiteral("HighPriority")))
                     .isEmpty());
        QVERIFY(!writeScript(sysLowDir, QStringLiteral("priority.luau"), validScript(QStringLiteral("LowPriority")))
                     .isEmpty());

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
        QVERIFY(!writeScript(userHomeDir, QStringLiteral("priority.luau"), validScript(QStringLiteral("UserVersion")))
                     .isEmpty());
        QVERIFY(!writeScript(sysHighDir, QStringLiteral("priority.luau"), validScript(QStringLiteral("HighSystem")))
                     .isEmpty());
        QVERIFY(!writeScript(sysLowDir, QStringLiteral("priority.luau"), validScript(QStringLiteral("LowSystem")))
                     .isEmpty());

        // `user_only` lives only in user dir; should be present.
        QVERIFY(!writeScript(userHomeDir, QStringLiteral("user_only.luau"), validScript(QStringLiteral("UserOnly")))
                     .isEmpty());

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

        // DATA_DIRS pinned alongside DATA_HOME per the file's convention: the
        // slot never scans, but an ambient value must not leak into any
        // path resolution the loader does at construction.
        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", xdgRoot.path().toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.ensureUserDirectoryExists();

        // The directory should now exist
        QVERIFY(QDir(expectedDir).exists());
    }
    /// The per-directory file cap bounds files CONSIDERED, not files kept.
    ///
    /// Counting survivors instead would let a directory sprayed with `*.luau`
    /// symlinks that ESCAPE the directory spend one `canonicalFilePath()`
    /// realpath syscall per entry on the GUI thread while never advancing the
    /// counter, so the cap would never trip — the exact GUI-thread DoS the
    /// guard names.
    ///
    /// Escaping symlinks specifically, not dangling ones: a dangling symlink is
    /// not a file, so `QDir::Files` drops it before the cap ever sees it and it
    /// costs nothing. An escaping symlink IS a file, so it is listed, charged,
    /// realpath'd, and only then refused by the containment check — which is
    /// precisely the "considered but not kept" case the counter has to charge
    /// for. Half the spray here is escaping symlinks, so a survivor-counting
    /// cap admits every real script past the limit and this slot fails.
    void testPerDirectoryCapBoundsFilesConsideredNotFilesKept()
    {
        XdgEnvGuard envGuard;
        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        const QString systemAlgoDir = xdgRoot.path() + QStringLiteral("/system/plasmazones/algorithms");
        const QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(systemAlgoDir));
        QVERIFY(QDir().mkpath(userAlgoDir));

        // MaxWatchedFilesPerDir is 100 and is not test-overridable, so the
        // fixture has to actually exceed it. Sorted names, because the cap
        // keeps the first `maxFiles` in QDir::Name order.
        // The loader's REAL cap, not a copy of its value. A hardcoded 100 would
        // stop exceeding the bound the moment the cap were raised, and the loop
        // below would then iterate over ids the cap never reached while the
        // non-vacuity check still passed.
        constexpr int kCap = PhosphorTiles::ScriptedAlgorithmLoader::MaxWatchedFilesPerDir;
        constexpr int kExtra = 10;
        // A real file OUTSIDE the scanned directory, for the escaping symlinks
        // to point at. It must exist, or QDir::Files would skip the links.
        const QString outsideTarget = xdgRoot.path() + QStringLiteral("/outside.luau");
        QVERIFY(!writeScript(xdgRoot.path(), QStringLiteral("outside.luau"), validScript(QStringLiteral("Outside")))
                     .isEmpty());
        QVERIFY(QFileInfo::exists(outsideTarget));

        for (int i = 0; i < kCap + kExtra; ++i) {
            const QString base = QStringLiteral("spray-%1").arg(i, 3, 10, QLatin1Char('0'));
            if (i % 2 == 0) {
                QVERIFY(!writeScript(userAlgoDir, base + QStringLiteral(".luau"), validScript(base)).isEmpty());
            } else {
                const QString linkPath = userAlgoDir + QLatin1Char('/') + base + QStringLiteral(".luau");
                QVERIFY(QFile::link(outsideTarget, linkPath));
            }
        }

        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user")).toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        // Exactly the real scripts among the first kCap entries survive. Every
        // real script past the cap must be absent: its presence means the cap
        // counted survivors and the spray bought unbounded work.
        QStringList admittedPastTheCap;
        for (int i = kCap; i < kCap + kExtra; ++i) {
            if (i % 2 != 0)
                continue;
            const QString id = QStringLiteral("script:spray-%1").arg(i, 3, 10, QLatin1Char('0'));
            if (registry->hasAlgorithm(id))
                admittedPastTheCap.append(id);
        }
        QVERIFY2(admittedPastTheCap.isEmpty(),
                 qPrintable(QStringLiteral("the cap counted survivors, not entries considered: %1")
                                .arg(admittedPastTheCap.join(QStringLiteral(", ")))));
        // Non-vacuity: the scan did register the real scripts below the cap, so
        // an empty result above is not just a scan that did nothing.
        QVERIFY(registry->hasAlgorithm(QStringLiteral("script:spray-000")));
    }

    /// A script EDITED in place between scans must be rebuilt, not reused.
    ///
    /// The reuse stamp keys on (id, size, mtime). Dropping the size/mtime half
    /// leaves "any previously stamped path is reused", which serves the OLD
    /// compiled script forever — invisible to the reuse slots above, because
    /// they only prove that reuse HAPPENS.
    void testEditedScriptIsRebuiltRatherThanReused()
    {
        XdgEnvGuard envGuard;
        QTemporaryDir xdgRoot;
        QVERIFY(xdgRoot.isValid());

        const QString systemAlgoDir = xdgRoot.path() + QStringLiteral("/system/plasmazones/algorithms");
        const QString userAlgoDir = xdgRoot.path() + QStringLiteral("/user/plasmazones/algorithms");
        QVERIFY(QDir().mkpath(systemAlgoDir));
        QVERIFY(QDir().mkpath(userAlgoDir));
        QVERIFY(!writeScript(userAlgoDir, QStringLiteral("mutable.luau"), validScript(QStringLiteral("BeforeEdit")))
                     .isEmpty());
        qputenv("XDG_DATA_DIRS", (xdgRoot.path() + QStringLiteral("/system")).toUtf8());
        qputenv("XDG_DATA_HOME", (xdgRoot.path() + QStringLiteral("/user")).toUtf8());

        PhosphorTiles::ScriptedAlgorithmLoader loader(QStringLiteral("plasmazones/algorithms"),
                                                      PlasmaZones::TestHelpers::testRegistry());
        loader.scanAndRegister();

        auto* registry = PlasmaZones::TestHelpers::testRegistry();
        const auto* before = registry->algorithm(QStringLiteral("script:mutable"));
        QVERIFY(before != nullptr);
        QCOMPARE(before->name(), QStringLiteral("BeforeEdit"));

        // Rewrite with a DIFFERENT display name and a different length, so the
        // stamp's size comparison alone is enough to notice even on a
        // filesystem with coarse mtime granularity.
        QVERIFY(!writeScript(userAlgoDir, QStringLiteral("mutable.luau"),
                             validScript(QStringLiteral("AfterEditWithALongerName")))
                     .isEmpty());
        loader.scanAndRegister();

        const auto* after = registry->algorithm(QStringLiteral("script:mutable"));
        QVERIFY(after != nullptr);
        QCOMPARE(after->name(), QStringLiteral("AfterEditWithALongerName"));
    }
};

QTEST_MAIN(TestScriptedAlgorithmLoader)
#include "test_scripted_algorithm_loader.moc"
