// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Golden parity harness: for each ported algorithm, the new Luau engine
// (LuauTileAlgorithm reading data/algorithms/NAME.luau) must produce byte-
// identical zones to the prior JS engine (ScriptedAlgorithm reading NAME.js)
// across a matrix of window counts, gaps, areas, and min-size variants.
//
// This is the gate for Phase 3: an algorithm is only "ported" once it is green
// here.

#include <QtTest>

#include <PhosphorTiles/LuauTileAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmWatchdog.h>
#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingParams.h>
#include <PhosphorTiles/TilingState.h>

#include <PhosphorScripting/LuauWatchdog.h>

#include <memory>

using namespace PhosphorTiles;

class TestLuauParity : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<ScriptedAlgorithmWatchdog> m_jsWatchdog;
    std::shared_ptr<PhosphorScripting::LuauWatchdog> m_luaWatchdog;

    static QString jsPath(const QString& name)
    {
        return QStringLiteral(PZ_SOURCE_DIR "/data/algorithms/") + name + QStringLiteral(".js");
    }
    static QString luaPath(const QString& name)
    {
        return QStringLiteral(PZ_SOURCE_DIR "/data/algorithms/") + name + QStringLiteral(".luau");
    }

    void compareAlgorithm(const QString& name);

private Q_SLOTS:
    void initTestCase()
    {
        m_jsWatchdog = std::make_shared<ScriptedAlgorithmWatchdog>();
        m_luaWatchdog = std::make_shared<PhosphorScripting::LuauWatchdog>();
    }

    // Batch 1
    void columns()
    {
        compareAlgorithm(QStringLiteral("columns"));
    }
    void rows()
    {
        compareAlgorithm(QStringLiteral("rows"));
    }
    void monocle()
    {
        compareAlgorithm(QStringLiteral("monocle"));
    }
    // Batch 2
    void deck()
    {
        compareAlgorithm(QStringLiteral("deck"));
    }
    void horizontalDeck()
    {
        compareAlgorithm(QStringLiteral("horizontal-deck"));
    }
    void masterStack()
    {
        compareAlgorithm(QStringLiteral("master-stack"));
    }
    void wide()
    {
        compareAlgorithm(QStringLiteral("wide"));
    }
    // Batch 3
    void paper()
    {
        compareAlgorithm(QStringLiteral("paper"));
    }
    void stair()
    {
        compareAlgorithm(QStringLiteral("stair"));
    }
    // Batch 4
    void zen()
    {
        compareAlgorithm(QStringLiteral("zen"));
    }
    void focusSidebar()
    {
        compareAlgorithm(QStringLiteral("focus-sidebar"));
    }
    // Batch 5
    void quadrantPriority()
    {
        compareAlgorithm(QStringLiteral("quadrant-priority"));
    }
    void cornerMaster()
    {
        compareAlgorithm(QStringLiteral("corner-master"));
    }
    // Batch 6
    void dwindle()
    {
        compareAlgorithm(QStringLiteral("dwindle"));
    }
    // Batch 7
    void spread()
    {
        compareAlgorithm(QStringLiteral("spread"));
    }
    void grid()
    {
        compareAlgorithm(QStringLiteral("grid"));
    }
    // Batch 8
    void threeColumn()
    {
        compareAlgorithm(QStringLiteral("three-column"));
    }
    void centeredMaster()
    {
        compareAlgorithm(QStringLiteral("centered-master"));
    }
    // Batch 9
    void cascade()
    {
        compareAlgorithm(QStringLiteral("cascade"));
    }
    void tatami()
    {
        compareAlgorithm(QStringLiteral("tatami"));
    }
    // Batch 10
    void spiral()
    {
        compareAlgorithm(QStringLiteral("spiral"));
    }
    // Batch 11
    void floatingCenter()
    {
        compareAlgorithm(QStringLiteral("floating-center"));
    }
    // dwindle-memory: only the stateless fallback is parity-tested. The JS
    // engine's tree path is dead (frozen-root leafCount bug), so the Luau tree
    // path is validated structurally in test_luau_tile_algorithm instead.
    void dwindleMemory()
    {
        compareAlgorithm(QStringLiteral("dwindle-memory"));
    }

    // Structural validation of the Luau dwindle-memory TREE path (which the JS
    // engine can't reach). Builds a known split tree and asserts the tree
    // geometry is used — distinguished from the stateless fallback by min-sizes,
    // which the tree path ignores by design.
    void dwindleMemoryTreePath()
    {
        LuauTileAlgorithm lua(luaPath(QStringLiteral("dwindle-memory")), m_luaWatchdog);
        QVERIFY(lua.isValid());
        QVERIFY(lua.supportsMemory());

        TilingState state(QStringLiteral("s"));
        for (int w = 0; w < 3; ++w) {
            state.addWindow(QStringLiteral("w%1").arg(w));
        }
        state.setSplitRatio(0.5);
        auto tree = std::make_unique<SplitTree>();
        for (int w = 0; w < 3; ++w) {
            tree->insertAtEnd(QStringLiteral("w%1").arg(w), 0.5);
        }
        QCOMPARE(tree->leafCount(), 3);
        state.setSplitTree(std::move(tree));

        TilingParams p;
        p.windowCount = 3;
        p.screenGeometry = QRect(0, 0, 1920, 1080);
        p.state = &state;
        p.innerGap = 0;
        p.outerGaps = EdgeGaps::uniform(0);
        // Window 0 asks for min width 1000. The stateless fallback would clamp the
        // first split up to 1000; the tree path ignores min sizes, so the 0.5
        // split (960) must survive — proving the tree path executed.
        p.minSizes = {QSize(1000, 100), QSize(100, 100), QSize(100, 100)};

        const QVector<QRect> zones = lua.calculateZones(p);
        QCOMPARE(zones.size(), 3);
        QCOMPARE(zones[0].width(), 960);
        // Zones tile the area without overlap.
        const QRect area(0, 0, 1920, 1080);
        for (int i = 0; i < zones.size(); ++i) {
            QVERIFY(area.contains(zones[i]));
            for (int j = i + 1; j < zones.size(); ++j) {
                QVERIFY(!zones[i].intersects(zones[j]));
            }
        }
    }
};

void TestLuauParity::compareAlgorithm(const QString& name)
{
    ScriptedAlgorithm js(jsPath(name), m_jsWatchdog);
    QVERIFY2(js.isValid(), qPrintable(QStringLiteral("JS %1 failed to load").arg(name)));
    LuauTileAlgorithm lua(luaPath(name), m_luaWatchdog);
    QVERIFY2(lua.isValid(), qPrintable(QStringLiteral("Luau %1 failed to load").arg(name)));

    const QList<QRect> areas = {QRect(0, 0, 1920, 1080), QRect(0, 0, 1366, 768), QRect(0, 0, 800, 1280)};
    const QList<int> gaps = {0, 8, 20};
    const QList<int> counts = {1, 2, 3, 4, 5, 6};
    const QList<qreal> ratios = {0.35, 0.6}; // master/stack split + deck focused fraction
    const QList<int> masterCounts = {1, 2};

    // Min-size variants: none, and a per-window set (truncated to count by the algos).
    const QVector<QSize> someMins = {QSize(200, 150), QSize(250, 120), QSize(180, 200),
                                     QSize(300, 100), QSize(220, 160), QSize(190, 140)};
    const QList<QVector<QSize>> minVariants = {QVector<QSize>{}, someMins};

    for (const QRect& area : areas) {
        for (int gap : gaps) {
            for (int count : counts) {
                for (qreal ratio : ratios) {
                    for (int masterCount : masterCounts) {
                        for (const QVector<QSize>& mins : minVariants) {
                            TilingState state(QStringLiteral("screen-1"));
                            for (int w = 0; w < count; ++w) {
                                state.addWindow(QStringLiteral("w%1").arg(w));
                            }
                            state.setSplitRatio(ratio);
                            state.setMasterCount(masterCount);
                            // addWindow lazily creates a split tree; drop it. The JS
                            // engine never reads a tree anyway (its leafCount is set on
                            // an already-frozen root and silently dropped), so a
                            // tree-free state is the only state JS and Luau agree on.
                            state.clearSplitTree();

                            TilingParams p;
                            p.windowCount = count;
                            p.screenGeometry = area;
                            p.state = &state;
                            p.innerGap = gap;
                            p.outerGaps = EdgeGaps::uniform(0);
                            p.minSizes = mins;

                            const QVector<QRect> jsZones = js.calculateZones(p);
                            const QVector<QRect> luaZones = lua.calculateZones(p);

                            if (jsZones != luaZones) {
                                qWarning().noquote()
                                    << "PARITY MISMATCH" << name << "area=" << area << "gap=" << gap
                                    << "count=" << count << "ratio=" << ratio << "mc=" << masterCount
                                    << "mins=" << mins.size() << "\n  js  =" << jsZones << "\n  luau=" << luaZones;
                            }
                            QCOMPARE(luaZones, jsZones);
                        }
                    }
                }
            }
        }
    }
}

QTEST_MAIN(TestLuauParity)
#include "test_luau_parity.moc"
