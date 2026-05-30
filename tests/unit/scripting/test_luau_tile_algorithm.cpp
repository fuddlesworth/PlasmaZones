// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Phase 2 exit criterion: a LuauTileAlgorithm runs end-to-end through the
// TilingAlgorithm interface — happy-path zone computation + metadata accessors,
// split-tree marshalling (memory), and lifecycle hooks.

#include <QtTest>

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

class TestLuauTileAlgorithm : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_watchdog;
    static QString scriptPath(const QString& name)
    {
        return QStringLiteral(PZ_LUAU_TEST_DIR "/data/") + name;
    }

private Q_SLOTS:
    void initTestCase()
    {
        m_watchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
    }

    void loadsAndExposesMetadata();
    void computesMasterStackZones();
    void marshalsSplitTree();
    void lifecycleHooksRunAndPersist();
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

QTEST_MAIN(TestLuauTileAlgorithm)
#include "test_luau_tile_algorithm.moc"
