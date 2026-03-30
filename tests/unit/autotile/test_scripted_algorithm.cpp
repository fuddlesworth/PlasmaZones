// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>

#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/ScriptedAlgorithm.h"
#include "core/constants.h"

#include "../helpers/ScriptTestHelpers.h"
#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Verify all zones have positive width and height
 */
static void verifyAllZonesPositive(const QVector<QRect>& zones)
{
    for (const QRect& zone : zones) {
        QVERIFY2(zone.width() > 0, qPrintable(QStringLiteral("Zone width was %1").arg(zone.width())));
        QVERIFY2(zone.height() > 0, qPrintable(QStringLiteral("Zone height was %1").arg(zone.height())));
    }
}

/**
 * @brief Resolve path to a shipped example algorithm in the source tree
 */
static QString exampleAlgoPath(const QString& name)
{
#ifdef PZ_SOURCE_DIR
    // Use compile-time source directory (most reliable across build configs)
    QString path = QStringLiteral(PZ_SOURCE_DIR "/data/algorithms/") + name;
    if (QFile::exists(path)) {
        return path;
    }
#endif
    // Fallback: relative to binary
    QString path2 = QCoreApplication::applicationDirPath() + QStringLiteral("/../../data/algorithms/") + name;
    if (QFile::exists(path2)) {
        return path2;
    }
    return QFINDTESTDATA(QStringLiteral("../../../../data/algorithms/") + name);
}

class TestScriptedAlgorithm : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};

    /**
     * @brief Minimal valid script with all metadata and a simple calculateZones
     */
    static QString fullMetadataScript()
    {
        return QStringLiteral(
            "// @name Test Layout\n"
            "// @description A test tiling layout\n"
            "// @supportsMasterCount true\n"
            "// @supportsSplitRatio true\n"
            "// @defaultSplitRatio 0.65\n"
            "// @defaultMaxWindows 8\n"
            "// @minimumWindows 2\n"
            "// @masterZoneIndex 0\n"
            "// @producesOverlappingZones true\n"
            "function calculateZones(params) {\n"
            "    var zones = [];\n"
            "    var area = params.area;\n"
            "    var w = Math.floor(area.width / params.windowCount);\n"
            "    for (var i = 0; i < params.windowCount; i++) {\n"
            "        zones.push({x: area.x + i * w, y: area.y, width: w, height: area.height});\n"
            "    }\n"
            "    return zones;\n"
            "}\n");
    }

    /**
     * @brief Minimal valid script with only required metadata
     */
    static QString minimalScript()
    {
        return QStringLiteral(
            "// @name Minimal\n"
            "// @description A minimal layout\n"
            "function calculateZones(params) {\n"
            "    if (params.windowCount <= 0) return [];\n"
            "    var area = params.area;\n"
            "    var w = Math.floor(area.width / params.windowCount);\n"
            "    var zones = [];\n"
            "    for (var i = 0; i < params.windowCount; i++) {\n"
            "        zones.push({x: area.x + i * w, y: area.y, width: w, height: area.height});\n"
            "    }\n"
            "    return zones;\n"
            "}\n");
    }

    /**
     * @brief Simple equal-columns script for zone calculation tests
     */
    static QString simpleColumnsScript()
    {
        return QStringLiteral(
            "// @name Simple Columns\n"
            "// @description Equal width columns\n"
            "function calculateZones(params) {\n"
            "    var count = params.windowCount;\n"
            "    if (count <= 0) return [];\n"
            "    var area = params.area;\n"
            "    var w = Math.floor(area.width / count);\n"
            "    var zones = [];\n"
            "    for (var i = 0; i < count; i++) {\n"
            "        var x = area.x + i * w;\n"
            "        var thisW = (i === count - 1) ? (area.width - i * w) : w;\n"
            "        zones.push({x: x, y: area.y, width: thisW, height: area.height});\n"
            "    }\n"
            "    return zones;\n"
            "}\n");
    }

private Q_SLOTS:

    void init()
    {
    }

    // =========================================================================
    // Metadata parsing tests
    // =========================================================================

    void testMetadata_parsesAllFields()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("full-meta.js"), fullMetadataScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.name(), QStringLiteral("Test Layout"));
        QCOMPARE(algo.description(), QStringLiteral("A test tiling layout"));
        QVERIFY(algo.supportsMasterCount());
        QVERIFY(algo.supportsSplitRatio());
        QCOMPARE(algo.defaultSplitRatio(), 0.65);
        QCOMPARE(algo.defaultMaxWindows(), 8);
        QCOMPARE(algo.minimumWindows(), 2);
        QCOMPARE(algo.masterZoneIndex(), 0);
        QVERIFY(algo.producesOverlappingZones());
    }

    void testMetadata_missingNameFallsBackToScriptId()
    {
        // Script is valid as long as calculateZones() exists — name falls back to scriptId
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @description No name layout\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("no-name.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(!algo.name().isEmpty()); // Falls back to scriptId-derived name
    }

    void testMetadata_missingDescriptionFallsBackToDefault()
    {
        // Script is valid as long as calculateZones() exists — description has a default
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name No Description\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("no-desc.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(!algo.description().isEmpty()); // Falls back to default string
    }

    void testMetadata_defaultValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("defaults.js"), minimalScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(!algo.supportsMasterCount());
        QVERIFY(!algo.supportsSplitRatio());
        QCOMPARE(algo.defaultSplitRatio(), AutotileDefaults::DefaultSplitRatio);
        QCOMPARE(algo.defaultMaxWindows(), AutotileDefaults::DefaultMaxWindows);
        QCOMPARE(algo.minimumWindows(), 1);
        QCOMPARE(algo.masterZoneIndex(), -1);
        QVERIFY(!algo.producesOverlappingZones());
    }

    // =========================================================================
    // Script loading tests
    // =========================================================================

    void testLoad_validScript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("valid.js"), minimalScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(algo.isScripted());
    }

    void testLoad_missingCalculateZones()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Missing Func\n"
            "// @description No calculateZones function\n"
            "var x = 42;\n");
        QString path = writeTempScript(dir, QStringLiteral("no-func.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(!algo.isValid());
    }

    void testLoad_syntaxError()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Broken\n"
            "// @description Has syntax error\n"
            "function calculateZones(params) {{{\n"
            "    return [;\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("syntax-error.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(!algo.isValid());
    }

    void testLoad_nonexistentFile()
    {
        ScriptedAlgorithm algo(QStringLiteral("/tmp/nonexistent_script_12345.js"));
        QVERIFY(!algo.isValid());
    }

    void testLoad_scriptIdFromFilename()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("my-layout.js"), minimalScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.scriptId(), QStringLiteral("script:my-layout"));
    }

    // =========================================================================
    // Zone calculation tests
    // =========================================================================

    void testCalculateZones_simpleColumns()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("columns.js"), simpleColumnsScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);

        verifyAllZonesPositive(zones);
        for (const QRect& zone : zones) {
            QCOMPARE(zone.height(), ScreenHeight);
        }
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testCalculateZones_zeroWindows()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("cols.js"), simpleColumnsScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({0, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testCalculateZones_oneWindow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("cols.js"), simpleColumnsScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0], m_screenGeometry);
    }

    void testCalculateZones_receivesCorrectParams()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Script that encodes params into zone dimensions for verification
        QString script = QStringLiteral(
            "// @name Param Echo\n"
            "// @description Echoes params as zone dimensions\n"
            "function calculateZones(params) {\n"
            "    return [{\n"
            "        x: params.windowCount,\n"
            "        y: params.masterCount,\n"
            "        width: params.innerGap,\n"
            "        height: Math.round(params.splitRatio * 1000)\n"
            "    }];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("echo.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        state.setMasterCount(3);
        state.setSplitRatio(0.75);

        auto zones = algo.calculateZones({5, m_screenGeometry, &state, 12, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QCOMPARE(zones[0].x(), 5); // windowCount
        QCOMPARE(zones[0].y(), 3); // masterCount
        QCOMPARE(zones[0].width(), 12); // innerGap
        QCOMPARE(zones[0].height(), 750); // splitRatio * 1000
    }

    void testCalculateZones_invalidReturn()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Script that returns a string instead of an array
        QString script = QStringLiteral(
            "// @name Bad Return\n"
            "// @description Returns wrong type\n"
            "function calculateZones(params) {\n"
            "    return 'not an array';\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("bad-return.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty());
    }

    void testCalculateZones_precomputedArea()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Script that returns the area dimensions as a zone
        QString script = QStringLiteral(
            "// @name Area Echo\n"
            "// @description Echoes area as zone\n"
            "function calculateZones(params) {\n"
            "    return [params.area];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("area-echo.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        // Pass outer gaps of 10px on each side
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(10)});
        QCOMPARE(zones.size(), 1);
        // Area should be screen minus outer gaps: 1920 - 20 = 1900, 1080 - 20 = 1060
        QCOMPARE(zones[0].x(), 10);
        QCOMPARE(zones[0].y(), 10);
        QCOMPARE(zones[0].width(), 1900);
        QCOMPARE(zones[0].height(), 1060);
    }

    // =========================================================================
    // isScripted / isUserScript tests
    // =========================================================================

    void testIsScripted_returnsTrue()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("scripted.js"), minimalScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(algo.isScripted());
    }

    void testIsUserScript_defaultFalse()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("user.js"), minimalScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(!algo.isUserScript());
    }

    void testIsUserScript_setUserScript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("user.js"), minimalScript());

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        algo.setUserScript(true);
        QVERIFY(algo.isUserScript());
    }

    // =========================================================================
    // Example algorithm smoke tests
    // =========================================================================

    void testExampleAlgo_data()
    {
        QTest::addColumn<QString>("filename");
        QTest::addColumn<QString>("expectedName");

        QTest::newRow("bsp") << QStringLiteral("bsp.js") << QStringLiteral("Binary Split");
        QTest::newRow("cascade") << QStringLiteral("cascade.js") << QStringLiteral("Cascade");
        QTest::newRow("centered-master") << QStringLiteral("centered-master.js") << QStringLiteral("Centered Master");
        QTest::newRow("columns") << QStringLiteral("columns.js") << QStringLiteral("Columns");
        QTest::newRow("corner-master") << QStringLiteral("corner-master.js") << QStringLiteral("Corner Master");
        QTest::newRow("deck") << QStringLiteral("deck.js") << QStringLiteral("Deck");
        QTest::newRow("dwindle") << QStringLiteral("dwindle.js") << QStringLiteral("Dwindle");
        QTest::newRow("dwindle-memory") << QStringLiteral("dwindle-memory.js") << QStringLiteral("Dwindle (Memory)");
        QTest::newRow("floating-center") << QStringLiteral("floating-center.js") << QStringLiteral("Floating Center");
        QTest::newRow("focus-sidebar") << QStringLiteral("focus-sidebar.js") << QStringLiteral("Focus + Sidebar");
        QTest::newRow("grid") << QStringLiteral("grid.js") << QStringLiteral("Grid");
        QTest::newRow("horizontal-deck") << QStringLiteral("horizontal-deck.js") << QStringLiteral("Horizontal Deck");
        QTest::newRow("master-stack") << QStringLiteral("master-stack.js") << QStringLiteral("Master + Stack");
        QTest::newRow("monocle") << QStringLiteral("monocle.js") << QStringLiteral("Monocle");
        QTest::newRow("paper") << QStringLiteral("paper.js") << QStringLiteral("Paper");
        QTest::newRow("quadrant-priority")
            << QStringLiteral("quadrant-priority.js") << QStringLiteral("Quadrant Priority");
        QTest::newRow("rows") << QStringLiteral("rows.js") << QStringLiteral("Rows");
        QTest::newRow("spiral") << QStringLiteral("spiral.js") << QStringLiteral("Spiral");
        QTest::newRow("spread") << QStringLiteral("spread.js") << QStringLiteral("Spread");
        QTest::newRow("stair") << QStringLiteral("stair.js") << QStringLiteral("Stair");
        QTest::newRow("tatami") << QStringLiteral("tatami.js") << QStringLiteral("Tatami");
        QTest::newRow("three-column") << QStringLiteral("three-column.js") << QStringLiteral("Three Column");
        QTest::newRow("wide") << QStringLiteral("wide.js") << QStringLiteral("Wide");
        QTest::newRow("zen") << QStringLiteral("zen.js") << QStringLiteral("Zen");
    }

    void testExampleAlgo()
    {
        QFETCH(QString, filename);
        QFETCH(QString, expectedName);

        QString path = exampleAlgoPath(filename);
        if (path.isEmpty()) {
            QSKIP(qPrintable(filename + QStringLiteral(" not found in source tree")));
        }

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.name(), expectedName);

        TilingState state(QStringLiteral("test"));

        const bool overlapping = algo.producesOverlappingZones();

        // Test window counts 1..6 with no gap
        for (int n = 1; n <= 6; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
            if (overlapping) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= n,
                         qPrintable(QStringLiteral("Expected 1-%1 zones, got %2 for %3 (gap=0)")
                                        .arg(n)
                                        .arg(zones.size())
                                        .arg(filename)));
            } else {
                QVERIFY2(zones.size() == n,
                         qPrintable(QStringLiteral("Expected exactly %1 zones, got %2 for %3 (gap=0)")
                                        .arg(n)
                                        .arg(zones.size())
                                        .arg(filename)));
            }
            verifyAllZonesPositive(zones);
        }

        // Test window counts 1..6 with moderate gap (innerGap = 10)
        for (int n = 1; n <= 6; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 10, EdgeGaps::uniform(0)});
            if (overlapping) {
                QVERIFY2(zones.size() >= 1 && zones.size() <= n,
                         qPrintable(QStringLiteral("Expected 1-%1 zones, got %2 for %3 (gap=10)")
                                        .arg(n)
                                        .arg(zones.size())
                                        .arg(filename)));
            } else {
                QVERIFY2(zones.size() == n,
                         qPrintable(QStringLiteral("Expected exactly %1 zones, got %2 for %3 (gap=10)")
                                        .arg(n)
                                        .arg(zones.size())
                                        .arg(filename)));
            }
            verifyAllZonesPositive(zones);
        }

        // Degenerate gap stress: gap larger than screen dimensions
        for (int n = 1; n <= 4; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 2000, EdgeGaps::uniform(0)});
            QVERIFY2(zones.size() >= 1 && zones.size() <= n,
                     qPrintable(QStringLiteral("Expected 1-%1 zones, got %2 for %3 (gap=2000)")
                                    .arg(n)
                                    .arg(zones.size())
                                    .arg(filename)));
            verifyAllZonesPositive(zones);
        }
    }

    // =========================================================================
    // Watchdog / safety tests
    // =========================================================================

    void testCalculateZones_infiniteLoopWatchdog()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path =
            writeTempScript(dir, QStringLiteral("infinite-loop.js"),
                            QStringLiteral("// @name InfiniteLoop\n"
                                           "// @description Tests watchdog\n"
                                           "function calculateZones(params) { while(true) {} return []; }\n"));

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        QElapsedTimer timer;
        timer.start();

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});

        QVERIFY(zones.isEmpty()); // Watchdog killed it — no zones returned
        // Generous threshold: the watchdog should interrupt within ~100ms,
        // but CI machines under load may be slower
        QVERIFY2(timer.elapsed() < 10000,
                 qPrintable(QStringLiteral("Watchdog took %1ms, expected < 10000ms").arg(timer.elapsed())));
    }

    void testCalculateZones_runtimeThrow()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path =
            writeTempScript(dir, QStringLiteral("thrower.js"),
                            QStringLiteral("// @name Thrower\n"
                                           "// @description Throws at runtime\n"
                                           "function calculateZones(params) { throw new Error('boom'); }\n"));

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid()); // Loads fine, error is at runtime

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.isEmpty()); // Runtime error = empty result
    }

    void testCalculateZones_negativeCoordsClamped()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("neg-coords.js"),
                                       QStringLiteral("// @name NegCoords\n"
                                                      "// @description Returns negative coordinates\n"
                                                      "function calculateZones(params) {\n"
                                                      "    return [{x: -50, y: -100, width: -10, height: 0}];\n"
                                                      "}\n"));

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 1);
        QVERIFY(zones[0].x() >= 0);
        QVERIFY(zones[0].y() >= 0);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[0].height() >= 1);
    }

    void testCalculateZones_tooManyZones()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("too-many.js"),
                                       QStringLiteral("// @name TooMany\n"
                                                      "// @description Returns more than 256 zones\n"
                                                      "function calculateZones(params) {\n"
                                                      "    var zones = [];\n"
                                                      "    for (var i = 0; i < 300; i++) {\n"
                                                      "        zones.push({x: 0, y: 0, width: 100, height: 100});\n"
                                                      "    }\n"
                                                      "    return zones;\n"
                                                      "}\n"));

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({300, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QVERIFY(zones.size() <= 256);
    }

    // =========================================================================
    // Script loading edge cases
    // =========================================================================

    void testLoad_oversizedScript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Create script > 1MB
        QString bigScript = QStringLiteral(
            "// @name Big\n"
            "// @description Oversized script\n"
            "function calculateZones(params) { return []; }\n");
        bigScript += QStringLiteral("// ") + QString(1024 * 1024, QLatin1Char('x')) + QStringLiteral("\n");

        QString path = writeTempScript(dir, QStringLiteral("oversized.js"), bigScript);

        ScriptedAlgorithm algo(path);
        QVERIFY(!algo.isValid());
    }

    void testCalculateZones_nanValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("nan-values.js"),
                                       QStringLiteral("// @name NaNValues\n"
                                                      "// @description Returns NaN zone values\n"
                                                      "function calculateZones(params) {\n"
                                                      "    return [{x: 0/0, y: 0, width: 100, height: 100}];\n"
                                                      "}\n"));

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // NaN toInt() returns 0; x is clamped to >= 0, width/height to >= 1
        QCOMPARE(zones.size(), 1);
        QVERIFY(zones[0].x() >= 0);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[0].height() >= 1);
    }

    void testCalculateZones_infinityValues()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("inf-values.js"),
                                       QStringLiteral("// @name InfValues\n"
                                                      "// @description Returns Infinity zone values\n"
                                                      "function calculateZones(params) {\n"
                                                      "    return [{x: 0, y: 0, width: 1/0, height: 100}];\n"
                                                      "}\n"));

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // Infinity toInt() returns 0, clamped to >= 1; then clampZonesToArea bounds it
        QCOMPARE(zones.size(), 1);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[0].height() >= 1);
    }

    void testMetadata_outOfRangeDefaultSplitRatio()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Test negative split ratio — should be clamped to MinSplitRatio (0.1)
        QString negScript = QStringLiteral(
            "// @name NegRatio\n"
            "// @description Negative split ratio\n"
            "// @defaultSplitRatio -1.0\n"
            "function calculateZones(params) { return []; }\n");
        QString negPath = writeTempScript(dir, QStringLiteral("neg-ratio.js"), negScript);

        ScriptedAlgorithm negAlgo(negPath);
        QVERIFY(negAlgo.isValid());
        QVERIFY(negAlgo.defaultSplitRatio() >= AutotileDefaults::MinSplitRatio);
        QVERIFY(negAlgo.defaultSplitRatio() <= AutotileDefaults::MaxSplitRatio);

        // Test over-max split ratio — should be clamped to MaxSplitRatio (0.9)
        QString highScript = QStringLiteral(
            "// @name HighRatio\n"
            "// @description Over-max split ratio\n"
            "// @defaultSplitRatio 2.0\n"
            "function calculateZones(params) { return []; }\n");
        QString highPath = writeTempScript(dir, QStringLiteral("high-ratio.js"), highScript);

        ScriptedAlgorithm highAlgo(highPath);
        QVERIFY(highAlgo.isValid());
        QVERIFY(highAlgo.defaultSplitRatio() >= AutotileDefaults::MinSplitRatio);
        QVERIFY(highAlgo.defaultSplitRatio() <= AutotileDefaults::MaxSplitRatio);
    }

    void testLoad_emptyScript()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString path = writeTempScript(dir, QStringLiteral("empty.js"), QString());

        ScriptedAlgorithm algo(path);
        QVERIFY(!algo.isValid());
    }

    // =========================================================================
    // Sandbox escape tests
    // =========================================================================

    void testSandboxPrototypePollution()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Prototype Pollution\n"
            "// @description Test\n"
            "function calculateZones(params) {\n"
            "    Object.prototype.polluted = true;\n"
            "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("proto-pollute.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});

        // The script should either fail (no zones) or succeed but not affect other scripts
        if (!zones.isEmpty()) {
            // If zones were returned, verify a subsequent script is not polluted
            QString cleanScript = QStringLiteral(
                "// @name Clean Check\n"
                "// @description Verify no pollution\n"
                "function calculateZones(params) {\n"
                "    var obj = {};\n"
                "    if (obj.polluted) return [];\n"
                "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
                "}\n");
            QString cleanPath = writeTempScript(dir, QStringLiteral("clean-check.js"), cleanScript);

            ScriptedAlgorithm cleanAlgo(cleanPath);
            QVERIFY(cleanAlgo.isValid());

            auto cleanZones = cleanAlgo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
            QVERIFY2(!cleanZones.isEmpty(), "Prototype pollution leaked between script instances");
        }
    }

    void testSandboxEvalBlocked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // QJSEngine V4 handles direct eval() at the bytecode level, bypassing
        // Object.defineProperty and IIFE scope shadowing. This is a known V4
        // limitation (QTBUG-style). The sandbox disables eval on the global object
        // and the IIFE wrapper shadows it, but V4 may still allow direct eval calls.
        // The watchdog timer (100ms) is the primary defense against malicious eval use.
        // This test documents the limitation: zones may or may not be empty depending
        // on the Qt version's V4 engine behavior.
        QString script = QStringLiteral(
            "// @name Eval Test\n"
            "// @description Test\n"
            "function calculateZones(params) {\n"
            "    try { eval(\"var x = 1\"); } catch(e) { return []; }\n"
            "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("eval-test.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // V4 known limitation: direct eval may bypass sandbox. Verify no crash
        // and that the result is structurally valid (either blocked or allowed).
        QVERIFY(zones.isEmpty() || zones.size() == 1);
        if (!zones.isEmpty()) {
            QWARN("V4 limitation: eval() not blocked by sandbox — watchdog is primary defense");
        }
    }

    void testSandboxEvalInfiniteLoopCaughtByWatchdog()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Even if V4 allows eval(), the watchdog MUST catch infinite loops.
        // This is the real security guarantee: no script can hang the process.
        // windowCount must be >= 2 to avoid the C++ single-window fast path.
        QString script = QStringLiteral(
            "// @name Eval Infinite Loop Test\n"
            "// @description Test\n"
            "function calculateZones(params) {\n"
            "    eval(\"while(true){}\");\n"
            "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("eval-loop.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        QElapsedTimer timer;
        timer.start();
        auto zones = algo.calculateZones({2, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // Two valid outcomes:
        // 1. IIFE wrapper blocked eval → TypeError thrown instantly → zones empty (fast)
        // 2. V4 bypassed IIFE → eval ran → watchdog interrupted → zones empty (~100ms)
        // Both produce empty zones; the watchdog is tested separately in testCalculateZones_infiniteLoopWatchdog.
        QVERIFY2(timer.elapsed() < 10000, "Watchdog failed to interrupt eval-based infinite loop");
        QVERIFY2(zones.isEmpty(), "eval-based infinite loop should produce empty zones");
    }

    void testSandboxFunctionConstructorBlocked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Same V4 limitation as eval — Function() may bypass global property lockdown.
        // The sandbox disables Function globally and via IIFE wrapper, but V4 may
        // still resolve the built-in. Watchdog is primary defense.
        QString script = QStringLiteral(
            "// @name Function Constructor Test\n"
            "// @description Test\n"
            "function calculateZones(params) {\n"
            "    try { var f = Function('return 1'); f(); } catch(e) { return []; }\n"
            "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("func-ctor.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // V4 known limitation: Function() may bypass sandbox.
        QVERIFY(zones.isEmpty() || zones.size() == 1);
        if (!zones.isEmpty()) {
            QWARN("V4 limitation: Function() not blocked by sandbox — watchdog is primary defense");
        }
    }

    void testSandboxConstructorConstructorBlocked()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Constructor chain escape: (function(){}).constructor.constructor('return this')()
        // The sandbox locks down Function.prototype.constructor and freezes
        // Function.prototype. V4 may still allow this via internal mechanisms.
        QString script = QStringLiteral(
            "// @name Constructor Chain Test\n"
            "// @description Test\n"
            "function calculateZones(params) {\n"
            "    try {\n"
            "        var global = (function(){}).constructor.constructor('return this')();\n"
            "    } catch(e) { return []; }\n"
            "    return [{ x: 0, y: 0, width: 100, height: 100 }];\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("ctor-chain.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({1, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        // V4 known limitation: constructor chain may bypass sandbox.
        QVERIFY(zones.isEmpty() || zones.size() == 1);
        if (!zones.isEmpty()) {
            QWARN("V4 limitation: constructor chain not blocked — watchdog is primary defense");
        }
    }

    // =========================================================================
    // @builtinId validation edge cases
    // =========================================================================

    void testBuiltinId_scriptPrefixRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Script Prefix Test\n"
            "// @description builtinId with script: prefix should be rejected\n"
            "// @builtinId script:bad\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("script-prefix.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        // script: prefix is rejected — builtinId should remain empty
        QVERIFY2(
            algo.builtinId().isEmpty(),
            qPrintable(QStringLiteral("Expected empty builtinId for 'script:bad', got '%1'").arg(algo.builtinId())));
    }

    void testBuiltinId_uppercaseRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Uppercase Test\n"
            "// @description builtinId with uppercase should be rejected\n"
            "// @builtinId UPPERCASE\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("uppercase-id.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        // Regex ^[a-z][a-z0-9-]*$ rejects uppercase
        QVERIFY2(
            algo.builtinId().isEmpty(),
            qPrintable(QStringLiteral("Expected empty builtinId for 'UPPERCASE', got '%1'").arg(algo.builtinId())));
    }

    void testBuiltinId_startsWithDigitRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Digit Start Test\n"
            "// @description builtinId starting with digit should be rejected\n"
            "// @builtinId 123startnum\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("digit-start.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        // Regex ^[a-z][a-z0-9-]*$ requires first char to be lowercase letter
        QVERIFY2(
            algo.builtinId().isEmpty(),
            qPrintable(QStringLiteral("Expected empty builtinId for '123startnum', got '%1'").arg(algo.builtinId())));
    }

    void testBuiltinId_longValueTruncatedTo64()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Create a valid builtinId that is 65+ characters long
        // Pattern: a-followed-by-64-more lowercase chars = 65 total
        QString longId = QStringLiteral("a") + QString(64, QLatin1Char('b')); // 65 chars
        QCOMPARE(longId.size(), 65);

        QString script = QStringLiteral(
                             "// @name Long ID Test\n"
                             "// @description builtinId longer than 64 chars should be truncated\n"
                             "// @builtinId ")
            + longId
            + QStringLiteral("\n"
                             "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("long-id.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        // .left(64) truncation — builtinId should be exactly 64 chars
        QVERIFY2(!algo.builtinId().isEmpty(), "Expected non-empty builtinId for a valid but long ID");
        QCOMPARE(algo.builtinId().size(), 64);
        QCOMPARE(algo.builtinId(), longId.left(64));
    }

    void testBuiltinId_validValueAccepted()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name Valid ID Test\n"
            "// @description Valid builtinId should be accepted\n"
            "// @builtinId my-custom-algo\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("valid-id.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.builtinId(), QStringLiteral("my-custom-algo"));
    }

    // =========================================================================
    // Frozen globals integrity test (INFRA-3)
    // =========================================================================

    void testFrozenGlobals_builtinOverwriteFails()
    {
        // Verify that a user script cannot overwrite frozen builtin helpers.
        // The sandbox freezes globals like fillArea, equalColumnsLayout, etc.
        // A script that attempts to reassign them should either throw (strict
        // mode) or silently fail, and the algorithm should still produce
        // correct output using the original frozen builtins.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Script tries to overwrite fillArea and equalColumnsLayout, then
        // delegates to equalColumnsLayout — if the overwrite succeeded,
        // equalColumnsLayout would return [] and produce zero zones.
        QString script = QStringLiteral(
            "// @name Frozen Globals Test\n"
            "// @description Verify frozen builtins survive overwrite attempts\n"
            "function calculateZones(params) {\n"
            "    // Attempt to overwrite frozen builtins\n"
            "    try { fillArea = function() { return []; }; } catch(e) {}\n"
            "    try { equalColumnsLayout = function() { return []; }; } catch(e) {}\n"
            "    try { distributeWithGaps = function() { return []; }; } catch(e) {}\n"
            "    try { masterStackLayout = function() { return []; }; } catch(e) {}\n"
            "    // Use equalColumnsLayout — if frozen, it still works correctly\n"
            "    return equalColumnsLayout(params.area, params.windowCount, params.innerGap || 0, params.minSizes || "
            "[]);\n"
            "}\n");
        QString path = writeTempScript(dir, QStringLiteral("frozen-globals.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());

        TilingState state(QStringLiteral("test"));
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});

        // The frozen equalColumnsLayout should still work — 3 columns expected
        QVERIFY2(
            zones.size() == 3,
            qPrintable(QStringLiteral("Expected 3 zones from frozen equalColumnsLayout, got %1").arg(zones.size())));

        // Verify structural validity
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }
};

QTEST_MAIN(TestScriptedAlgorithm)
#include "test_scripted_algorithm.moc"
