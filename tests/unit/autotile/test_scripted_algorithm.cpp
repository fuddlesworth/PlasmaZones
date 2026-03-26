// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QTemporaryDir>
#include <QFile>

#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/ScriptedAlgorithm.h"
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

/**
 * @brief Helper to write a temporary JS script file
 */
static QString writeTempScript(QTemporaryDir& dir, const QString& filename, const QString& content)
{
    QString path = dir.path() + QStringLiteral("/") + filename;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return path;
    f.write(content.toUtf8());
    f.close();
    return path;
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
            "// @icon view-grid-symbolic\n"
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
            "// @icon view-list\n"
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
            "// @icon view-split-left-right\n"
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
        QCOMPARE(algo.icon(), QStringLiteral("view-grid-symbolic"));
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
            "// @icon view-list\n"
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
            "// @icon view-list\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("no-desc.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(!algo.description().isEmpty()); // Falls back to default string
    }

    void testMetadata_missingIconUsesDefault()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QString script = QStringLiteral(
            "// @name No Icon\n"
            "// @description Layout without icon\n"
            "function calculateZones(params) { return []; }\n");
        QString path = writeTempScript(dir, QStringLiteral("no-icon.js"), script);

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QVERIFY(!algo.icon().isEmpty());
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
            "// @icon view-list\n"
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
            "// @icon view-list\n"
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
        QCOMPARE(algo.scriptId(), QStringLiteral("my-layout"));
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

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
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
            "// @icon view-list\n"
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
            "// @icon view-list\n"
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
            "// @icon view-list\n"
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

    void testExampleAlgo_deck()
    {
        QString path = exampleAlgoPath(QStringLiteral("deck.js"));
        if (path.isEmpty()) {
            QSKIP("deck.js not found in source tree");
        }

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.name(), QStringLiteral("Deck"));

        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.05);

        for (int n = 1; n <= 4; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), n);
            for (const QRect& zone : zones) {
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }

    void testExampleAlgo_tatami()
    {
        QString path = exampleAlgoPath(QStringLiteral("tatami.js"));
        if (path.isEmpty()) {
            QSKIP("tatami.js not found in source tree");
        }

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.name(), QStringLiteral("Tatami"));

        TilingState state(QStringLiteral("test"));

        for (int n = 1; n <= 4; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), n);
            for (const QRect& zone : zones) {
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }

    void testExampleAlgo_goldenRatio()
    {
        QString path = exampleAlgoPath(QStringLiteral("golden-ratio.js"));
        if (path.isEmpty()) {
            QSKIP("golden-ratio.js not found in source tree");
        }

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.name(), QStringLiteral("Golden Ratio"));

        TilingState state(QStringLiteral("test"));

        for (int n = 1; n <= 4; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), n);
            for (const QRect& zone : zones) {
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }

    void testExampleAlgo_paper()
    {
        QString path = exampleAlgoPath(QStringLiteral("paper.js"));
        if (path.isEmpty()) {
            QSKIP("paper.js not found in source tree");
        }

        ScriptedAlgorithm algo(path);
        QVERIFY(algo.isValid());
        QCOMPARE(algo.name(), QStringLiteral("Paper"));

        TilingState state(QStringLiteral("test"));
        state.setSplitRatio(0.8);

        for (int n = 1; n <= 4; ++n) {
            auto zones = algo.calculateZones({n, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
            QCOMPARE(zones.size(), n);
            for (const QRect& zone : zones) {
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }
};

QTEST_MAIN(TestScriptedAlgorithm)
#include "test_scripted_algorithm.moc"
