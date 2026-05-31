// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 2 exit criterion: a LuauTileAlgorithm runs end-to-end through the
// TilingAlgorithm interface — happy-path zone computation + metadata accessors,
// split-tree marshalling (memory), and lifecycle hooks.

#include <QtTest>

#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include "../helpers/ScriptTestHelpers.h"

#include <QTemporaryDir>

#include <memory>

using namespace PhosphorTiles;

class TestLuauTileAlgorithm : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;
    QTemporaryDir m_tmp;
    static QString scriptPath(const QString& name)
    {
        return QStringLiteral(PZ_LUAU_TEST_DIR "/data/") + name;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_watchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
        QVERIFY(m_tmp.isValid());
    }

    void loadsAndExposesMetadata();
    void computesMasterStackZones();
    void marshalsSplitTree();
    void lifecycleHooksRunAndPersist();
    void metadataOutOfRangeValuesClamped();
    void customParamInvertedRangeNormalized();
    void malformedZonesKeepCountAndStayInArea();
    void missingTileFunctionIsInvalid();
    void runtimeErrorYieldsEmptyZones();
};

void TestLuauTileAlgorithm::loadsAndExposesMetadata()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("master-stack.luau")), m_watchdog);
    QVERIFY(algo.isValid());
    QCOMPARE(algo.name(), QStringLiteral("Master + Stack"));
    QCOMPARE(algo.id(), QStringLiteral("master-stack"));
    QVERIFY(algo.supportsMasterCount());
    QVERIFY(algo.supportsSplitRatio());
    QVERIFY(algo.isScripted());
    QCOMPARE(algo.defaultSplitRatio(), 0.6);
}

void TestLuauTileAlgorithm::computesMasterStackZones()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("master-stack.luau")), m_watchdog);
    QVERIFY(algo.isValid());

    TilingState state(QStringLiteral("screen-1"));
    state.setSplitRatio(0.6);

    TilingParams p;
    p.windowCount = 5;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state;
    p.innerGap = 8;
    p.outerGaps = EdgeGaps::uniform(0); // full screen is the usable area

    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 5);

    // First zone is the master column: ~60% of the 1920 width, full height.
    QVERIFY(zones[0].width() > 1100 && zones[0].width() < 1160);
    QCOMPARE(zones[0].height(), 1080);
    // Every zone stays inside the screen and none overlap.
    const QRect area(0, 0, 1920, 1080);
    for (int i = 0; i < zones.size(); ++i) {
        QVERIFY(area.contains(zones[i]));
        for (int j = i + 1; j < zones.size(); ++j) {
            QVERIFY(!zones[i].intersects(zones[j]));
        }
    }
}

void TestLuauTileAlgorithm::marshalsSplitTree()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("memory-hooks.luau")), m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsMemory());

    TilingState state(QStringLiteral("screen-1"));
    auto tree = std::make_unique<SplitTree>();
    tree->insertAtEnd(QStringLiteral("w1"));
    tree->insertAtEnd(QStringLiteral("w2"));
    tree->insertAtEnd(QStringLiteral("w3"));
    QCOMPARE(tree->leafCount(), 3);
    state.setSplitTree(std::move(tree));

    TilingParams p;
    p.windowCount = 5; // intentionally != leafCount
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state;
    p.innerGap = 8;

    // The script returns one zone per tree leaf — so a count of 3 proves the
    // tree (and its leafCount) marshalled across correctly.
    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 3);
}

void TestLuauTileAlgorithm::lifecycleHooksRunAndPersist()
{
    LuauTileAlgorithm algo(scriptPath(QStringLiteral("memory-hooks.luau")), m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsLifecycleHooks());

    TilingState state(QStringLiteral("screen-1"));
    state.addWindow(QStringLiteral("a"));
    state.addWindow(QStringLiteral("b"));

    // Fire the hook twice; the module-local counter must persist between calls.
    algo.onWindowAdded(&state, 0);
    algo.onWindowAdded(&state, 1);

    TilingParams p;
    p.windowCount = 4;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.state = &state; // no split tree → tile() returns `added` zones
    p.innerGap = 8;

    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 2); // added == 2
}

void TestLuauTileAlgorithm::metadataOutOfRangeValuesClamped()
{
    using namespace PlasmaZones::TestHelpers;
    using namespace AutotileDefaults;
    // Raw metadata table (bypassing pz.algorithm) so the C++ parse + clamp path
    // sees the out-of-range values directly.
    const QString path = writeTempScript(m_tmp, QStringLiteral("clamp.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "clamp-test", name = "Clamp Test",
                defaultSplitRatio = 5.0,   -- above MaxSplitRatio
                minimumWindows = 9999,     -- above MaxMetadataWindows
                defaultMaxWindows = 9999,  -- above MaxMetadataWindows
            },
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    // The bogus 5.0 ratio is pulled back into the legal split range.
    QVERIFY(algo.defaultSplitRatio() >= MinSplitRatio && algo.defaultSplitRatio() <= MaxSplitRatio);
    QVERIFY(algo.defaultSplitRatio() < 5.0);
    QVERIFY(algo.minimumWindows() >= MinMetadataWindows && algo.minimumWindows() <= MaxMetadataWindows);
    QVERIFY(algo.minimumWindows() < 9999);
    QVERIFY(algo.defaultMaxWindows() >= MinMetadataWindows && algo.defaultMaxWindows() <= MaxMetadataWindows);
    QVERIFY(algo.defaultMaxWindows() < 9999);
}

void TestLuauTileAlgorithm::customParamInvertedRangeNormalized()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("cp.luau"), QStringLiteral(R"LUA(
        return {
            metadata = {
                id = "cp-test", name = "CP Test",
                customParams = {
                    { name = "ratio", type = "number", min = 1.0, max = 0.0, default = 0.5 },
                },
            },
            tile = function(ctx) return {} end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());
    QVERIFY(algo.supportsCustomParams());
    const QVariantList defs = algo.customParamDefList();
    QCOMPARE(defs.size(), 1);
    const QVariantMap cp = defs.at(0).toMap();
    // The inverted [1.0, 0.0] range must have been swapped to [0.0, 1.0].
    QVERIFY(cp.value(QStringLiteral("minValue")).toDouble() <= cp.value(QStringLiteral("maxValue")).toDouble());
}

void TestLuauTileAlgorithm::malformedZonesKeepCountAndStayInArea()
{
    using namespace PlasmaZones::TestHelpers;
    // tile() returns two malformed entries: an empty map and one with negative
    // dimensions. Both must survive as one zone each (count stays aligned with
    // the window count) and be clamped back inside the area.
    const QString path = writeTempScript(m_tmp, QStringLiteral("malformed.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "malformed", name = "Malformed" },
            tile = function(ctx)
                return { {}, { x = -100, y = 0, width = -5, height = 10 } }
            end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid());

    TilingParams p;
    p.windowCount = 2;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.outerGaps = EdgeGaps::uniform(0);

    const QRect area(0, 0, 1920, 1080);
    const QVector<QRect> zones = algo.calculateZones(p);
    QCOMPARE(zones.size(), 2); // not collapsed to fewer than the window count
    for (const QRect& z : zones) {
        QVERIFY(!z.isEmpty());
        QVERIFY(area.contains(z));
    }
}

void TestLuauTileAlgorithm::missingTileFunctionIsInvalid()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("notile.luau"), QStringLiteral(R"LUA(
        return { metadata = { id = "notile", name = "No Tile" } }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(!algo.isValid());
}

void TestLuauTileAlgorithm::runtimeErrorYieldsEmptyZones()
{
    using namespace PlasmaZones::TestHelpers;
    const QString path = writeTempScript(m_tmp, QStringLiteral("boom.luau"), QStringLiteral(R"LUA(
        return {
            metadata = { id = "boom", name = "Boom" },
            tile = function(ctx) error("kaboom") end,
        }
    )LUA"));
    QVERIFY(!path.isEmpty());

    LuauTileAlgorithm algo(path, m_watchdog);
    QVERIFY(algo.isValid()); // loads fine; the error only happens when tile() runs

    TilingParams p;
    p.windowCount = 3;
    p.screenGeometry = QRect(0, 0, 1920, 1080);
    p.outerGaps = EdgeGaps::uniform(0);

    // A runtime error inside tile() degrades to no zones, never a crash.
    QVERIFY(algo.calculateZones(p).isEmpty());
}

QTEST_MAIN(TestLuauTileAlgorithm)
#include "test_luau_tile_algorithm.moc"
