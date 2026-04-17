// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QJsonObject>
#include <QRect>
#include <QVector>

#include "autotile/SplitTree.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/AlgorithmRegistry.h"
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"
#include "../helpers/ScriptedAlgoTestSetup.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestSplitTree : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};
    ScriptedAlgoTestSetup m_scriptSetup;

    TilingAlgorithm* dwindleMemory()
    {
        return AlgorithmRegistry::instance()->algorithm(QLatin1String("dwindle-memory"));
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(PZ_SOURCE_DIR)));
        QVERIFY(dwindleMemory() != nullptr);
    }

    // =========================================================================
    // SplitTree core tests
    // =========================================================================

    void testEmpty()
    {
        SplitTree tree;
        QVERIFY(tree.isEmpty());
        QCOMPARE(tree.leafCount(), 0);
        QVERIFY(tree.root() == nullptr);
        QVERIFY(tree.leafOrder().isEmpty());

        auto zones = tree.applyGeometry(m_screenGeometry, 0);
        QVERIFY(zones.isEmpty());
    }

    void testInsertFirst()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));

        QVERIFY(!tree.isEmpty());
        QCOMPARE(tree.leafCount(), 1);
        QCOMPARE(tree.leafOrder(), QStringList{QStringLiteral("win1")});

        auto* root = tree.root();
        QVERIFY(root != nullptr);
        QVERIFY(root->isLeaf());
        QCOMPARE(root->windowId, QStringLiteral("win1"));
    }

    void testInsertTwo()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        QCOMPARE(tree.leafCount(), 2);
        QCOMPARE(tree.leafOrder(), (QStringList{QStringLiteral("win1"), QStringLiteral("win2")}));

        auto* root = tree.root();
        QVERIFY(root != nullptr);
        QVERIFY(!root->isLeaf());
    }

    void testInsertThree()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        QCOMPARE(tree.leafCount(), 3);
        QCOMPARE(tree.leafOrder(),
                 (QStringList{QStringLiteral("win1"), QStringLiteral("win2"), QStringLiteral("win3")}));
    }

    void testInsertAtFocused()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtFocused(QStringLiteral("win3"), QStringLiteral("win1"));

        QCOMPARE(tree.leafCount(), 3);

        // win3 should appear after win1 (win1's leaf was split, win1 stays first, win3 second)
        auto order = tree.leafOrder();
        QCOMPARE(order.size(), 3);
        int win1Idx = order.indexOf(QStringLiteral("win1"));
        int win3Idx = order.indexOf(QStringLiteral("win3"));
        QVERIFY(win1Idx >= 0);
        QVERIFY(win3Idx >= 0);
        QCOMPARE(win3Idx, win1Idx + 1);
    }

    void testInsertAtPosition()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));
        tree.insertAtPosition(QStringLiteral("win4"), 1);

        QCOMPARE(tree.leafCount(), 4);

        // insertAtPosition splits the leaf at position 1 (win2).
        // The existing window stays as first child, new window becomes second.
        // Result: [win1, win2, win4, win3]
        auto order = tree.leafOrder();
        QCOMPARE(order.size(), 4);
        QCOMPARE(order[0], QStringLiteral("win1"));
        QCOMPARE(order[1], QStringLiteral("win2"));
        QCOMPARE(order[2], QStringLiteral("win4"));
        QCOMPARE(order[3], QStringLiteral("win3"));
    }

    void testRemoveLeafFromTwo()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.remove(QStringLiteral("win1"));

        QCOMPARE(tree.leafCount(), 1);
        QCOMPARE(tree.leafOrder(), QStringList{QStringLiteral("win2")});

        auto* root = tree.root();
        QVERIFY(root != nullptr);
        QVERIFY(root->isLeaf());
        QCOMPARE(root->windowId, QStringLiteral("win2"));
    }

    void testRemoveMiddlePreservesRatios()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        // Resize the split associated with win2 to 0.7
        tree.resizeSplit(QStringLiteral("win2"), 0.7);

        tree.remove(QStringLiteral("win2"));

        QCOMPARE(tree.leafCount(), 2);
        QCOMPARE(tree.leafOrder(), (QStringList{QStringLiteral("win1"), QStringLiteral("win3")}));

        // After removing win2, its parent node is collapsed (sibling promoted).
        // The root should still exist with a valid ratio.
        QVERIFY(tree.root() != nullptr);
        QVERIFY(tree.root()->splitRatio > 0.0);
        QVERIFY(tree.root()->splitRatio < 1.0);
    }

    void testRemoveLastWindow()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.remove(QStringLiteral("win1"));

        QVERIFY(tree.isEmpty());
        QCOMPARE(tree.leafCount(), 0);
        QVERIFY(tree.root() == nullptr);
    }

    void testSwap()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        auto orderBefore = tree.leafOrder();
        tree.swap(QStringLiteral("win1"), QStringLiteral("win3"));
        auto orderAfter = tree.leafOrder();

        // After swap, positions should be exchanged
        QCOMPARE(orderAfter.size(), 3);
        QCOMPARE(orderAfter[0], QStringLiteral("win3"));
        QCOMPARE(orderAfter[1], QStringLiteral("win2"));
        QCOMPARE(orderAfter[2], QStringLiteral("win1"));

        // Structure unchanged: leaf count and root type remain the same
        QCOMPARE(tree.leafCount(), 3);
        QVERIFY(!tree.root()->isLeaf());
    }

    void testResizeSplit()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        tree.resizeSplit(QStringLiteral("win2"), 0.3);

        auto* root = tree.root();
        QVERIFY(root != nullptr);
        QVERIFY(!root->isLeaf());
        QCOMPARE(root->splitRatio, 0.3);
    }

    // =========================================================================
    // Geometry tests
    // =========================================================================

    void testApplyGeometry_twoWindows()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        auto zones = tree.applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zones.size(), 2);

        // Default ratio is 0.5, so each zone should be ~960px wide
        QCOMPARE(zones[0].width() + zones[1].width(), ScreenWidth);
        QVERIFY(zones[0].width() > 0);
        QVERIFY(zones[1].width() > 0);
        QCOMPARE(zones[0].height(), ScreenHeight);
        QCOMPARE(zones[1].height(), ScreenHeight);

        QVERIFY(noOverlaps(zones));
        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testApplyGeometry_threeWindows()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        auto zones = tree.applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zones.size(), 3);

        for (const QRect& zone : zones) {
            QVERIFY(zone.isValid());
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }

        QVERIFY(allWithinBounds(zones, m_screenGeometry));
    }

    void testApplyGeometry_withGaps()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        auto zones = tree.applyGeometry(m_screenGeometry, 10);
        QCOMPARE(zones.size(), 2);

        // Zones should not overlap
        QVERIFY(noOverlaps(zones));

        // Both zones should remain within screen bounds
        QVERIFY(allWithinBounds(zones, m_screenGeometry));

        // Zones should have valid positive dimensions
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    // =========================================================================
    // Serialization tests
    // =========================================================================

    void testSerializationRoundTrip()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));
        tree.resizeSplit(QStringLiteral("win2"), 0.7);

        QJsonObject json = tree.toJson();
        auto restored = SplitTree::fromJson(json);
        QVERIFY(restored != nullptr);

        QCOMPARE(restored->leafCount(), tree.leafCount());
        QCOMPARE(restored->leafOrder(), tree.leafOrder());

        // Verify structural properties are preserved
        QCOMPARE(restored->root()->splitHorizontal, tree.root()->splitHorizontal);
        QVERIFY(qFuzzyCompare(restored->root()->splitRatio, tree.root()->splitRatio));

        // Verify geometry output matches (proves ratios are preserved)
        auto zonesOriginal = tree.applyGeometry(m_screenGeometry, 0);
        auto zonesRestored = restored->applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zonesOriginal, zonesRestored);
    }

    void testSerializationRoundTripDeep()
    {
        SplitTree tree;
        // Build an 8-window tree
        for (int i = 1; i <= 8; i++) {
            tree.insertAtEnd(QStringLiteral("win%1").arg(i));
        }
        // Resize some splits to vary ratios
        tree.resizeSplit(QStringLiteral("win2"), 0.3);
        tree.resizeSplit(QStringLiteral("win5"), 0.7);
        // Swap two windows
        tree.swap(QStringLiteral("win3"), QStringLiteral("win6"));

        // Serialize
        QJsonObject json = tree.toJson();

        // Deserialize
        auto restored = SplitTree::fromJson(json);
        QVERIFY(restored);

        // Verify leaf count
        QCOMPARE(restored->leafCount(), 8);

        // Verify leaf order matches
        QCOMPARE(restored->leafOrder(), tree.leafOrder());

        // Verify round-trip produces identical JSON
        QJsonObject json2 = restored->toJson();
        QCOMPARE(json2, json);

        // Verify geometry output matches (proves ratios are preserved)
        auto zonesOriginal = tree.applyGeometry(m_screenGeometry, 0);
        auto zonesRestored = restored->applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zonesOriginal.size(), zonesRestored.size());
        QCOMPARE(zonesOriginal, zonesRestored);

        // Verify geometry with gaps also matches
        auto zonesGapOriginal = tree.applyGeometry(m_screenGeometry, 8);
        auto zonesGapRestored = restored->applyGeometry(m_screenGeometry, 8);
        QCOMPARE(zonesGapOriginal, zonesGapRestored);
    }

    void testSerializationEmpty()
    {
        SplitTree tree;
        QJsonObject json = tree.toJson();
        auto restored = SplitTree::fromJson(json);

        // Restoring an empty tree should either return nullptr or an empty tree
        if (restored) {
            QVERIFY(restored->isEmpty());
            QCOMPARE(restored->leafCount(), 0);
        }
    }

    // =========================================================================
    // Lookup tests
    // =========================================================================

    void testLeafForWindow()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        auto* leaf = tree.leafForWindow(QStringLiteral("win2"));
        QVERIFY(leaf != nullptr);
        QVERIFY(leaf->isLeaf());
        QCOMPARE(leaf->windowId, QStringLiteral("win2"));

        auto* notFound = tree.leafForWindow(QStringLiteral("nonexistent"));
        QVERIFY(notFound == nullptr);
    }

    // =========================================================================
    // DwindleMemoryAlgorithm tests
    // =========================================================================

    void testAlgo_withTree()
    {
        auto* algo = dwindleMemory();
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        auto tree = std::make_unique<SplitTree>();
        tree->insertAtEnd(QStringLiteral("win1"));
        tree->insertAtEnd(QStringLiteral("win2"));
        tree->insertAtEnd(QStringLiteral("win3"));
        state.setSplitTree(std::move(tree));

        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);

        for (const QRect& zone : zones) {
            QVERIFY(zone.isValid());
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testAlgo_withoutTree()
    {
        auto* algo = dwindleMemory();
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // No split tree set — should fall back to stateless dwindle
        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testAlgo_treeCountMismatch()
    {
        auto* algo = dwindleMemory();
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // Tree has only 2 leaves but state has 3 windows — should fall back
        auto tree = std::make_unique<SplitTree>();
        tree->insertAtEnd(QStringLiteral("win1"));
        tree->insertAtEnd(QStringLiteral("win2"));
        state.setSplitTree(std::move(tree));

        auto zones =
            algo->calculateZones(makeParams(3, m_screenGeometry, &state, 0, ::PhosphorLayout::EdgeGaps::uniform(0)));
        QCOMPARE(zones.size(), 3);
    }

    void testAlgo_metadata()
    {
        auto* algo = dwindleMemory();
        QVERIFY(!algo->name().isEmpty());
        QVERIFY(!algo->description().isEmpty());
        QVERIFY(algo->supportsSplitRatio());
        QVERIFY(algo->supportsMemory());
    }

    void testAlgo_prepareTilingState_createsTree()
    {
        auto* algo = dwindleMemory();
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // TilingState::addWindow triggers syncTreeLazyCreate, so tree exists
        // after the 2nd window. Clear it to test prepareTilingState from scratch.
        state.clearSplitTree();
        QVERIFY(state.splitTree() == nullptr);

        // prepareTilingState should create a tree with 3 leaves
        algo->prepareTilingState(&state);
        QVERIFY(state.splitTree() != nullptr);
        QCOMPARE(state.splitTree()->leafCount(), 3);
    }

    void testAlgo_prepareTilingState_skipsIfTreeExists()
    {
        auto* algo = dwindleMemory();
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));

        // Manually set a tree with custom ratio
        auto tree = std::make_unique<SplitTree>();
        tree->insertAtEnd(QStringLiteral("win1"), 0.7);
        tree->insertAtEnd(QStringLiteral("win2"), 0.7);
        state.setSplitTree(std::move(tree));

        const auto* originalTree = state.splitTree();

        // prepareTilingState should not replace existing tree
        algo->prepareTilingState(&state);
        QVERIFY(state.splitTree() == originalTree);
    }

    void testAlgo_prepareTilingState_skipsSingleWindow()
    {
        auto* algo = dwindleMemory();
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        algo->prepareTilingState(&state);
        QVERIFY(state.splitTree() == nullptr);
    }

    // =========================================================================
    // Corrupted JSON deserialization tests
    // =========================================================================

    void testFromJson_emptyObject()
    {
        QJsonObject json;
        // Empty object — no "type", no children
        auto tree = SplitTree::fromJson(json);
        // Should not crash; returns nullptr or an empty tree
        if (tree) {
            QCOMPARE(tree->leafCount(), 0);
        }
    }

    void testFromJson_wrongTypes()
    {
        // A leaf with wrong value types, wrapped in {"root": ...}
        QJsonObject leaf;
        leaf[QStringLiteral("windowId")] = 42; // wrong type — should be string

        QJsonObject wrapper;
        wrapper[QStringLiteral("root")] = leaf;

        auto tree = SplitTree::fromJson(wrapper);
        // QJsonValue::toString() on int returns empty string (not "42"),
        // so the leaf is rejected due to empty windowId — tree has no leaves.
        // The tree may be null or empty depending on implementation.
        QVERIFY(!tree || tree->leafCount() == 0);

        // An internal node with wrong ratio type
        QJsonObject firstLeaf;
        firstLeaf[QStringLiteral("windowId")] = QStringLiteral("win1");

        QJsonObject secondLeaf;
        secondLeaf[QStringLiteral("windowId")] = QStringLiteral("win2");

        QJsonObject internal;
        internal[QStringLiteral("ratio")] = QStringLiteral("not a number"); // wrong type
        internal[QStringLiteral("horizontal")] = true;
        internal[QStringLiteral("first")] = firstLeaf;
        internal[QStringLiteral("second")] = secondLeaf;

        QJsonObject wrapper2;
        wrapper2[QStringLiteral("root")] = internal;

        auto tree2 = SplitTree::fromJson(wrapper2);
        // toDouble() on a string returns 0.0, which gets clamped to MinSplitRatio.
        // The tree should still parse successfully with 2 leaves.
        QVERIFY(tree2 != nullptr);
        QCOMPARE(tree2->leafCount(), 2);
    }

    void testFromJson_negativeRatio()
    {
        // Build a valid internal node with negative ratio
        QJsonObject firstLeaf;
        firstLeaf[QStringLiteral("windowId")] = QStringLiteral("win1");

        QJsonObject secondLeaf;
        secondLeaf[QStringLiteral("windowId")] = QStringLiteral("win2");

        QJsonObject node;
        node[QStringLiteral("ratio")] = -0.5; // invalid — should be clamped
        node[QStringLiteral("horizontal")] = true;
        node[QStringLiteral("first")] = firstLeaf;
        node[QStringLiteral("second")] = secondLeaf;

        QJsonObject wrapper;
        wrapper[QStringLiteral("root")] = node;

        auto tree = SplitTree::fromJson(wrapper);
        // Ratio should be clamped to MinSplitRatio; tree must be usable
        QVERIFY(tree);
        QCOMPARE(tree->leafCount(), 2);
        // Geometry should not crash with clamped ratio
        auto zones = tree->applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zones.size(), 2);
        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testFromJson_deeplyNested()
    {
        // Build a tree deeper than MaxDeserializationDepth (AutotileDefaults::MaxRuntimeTreeDepth = 50)
        QJsonObject leaf;
        leaf[QStringLiteral("windowId")] = QStringLiteral("win1");

        QJsonObject current = leaf;
        for (int i = 0; i < 55; ++i) {
            QJsonObject secondLeaf;
            secondLeaf[QStringLiteral("windowId")] = QStringLiteral("win%1").arg(i + 2);

            QJsonObject parent;
            parent[QStringLiteral("ratio")] = 0.5;
            parent[QStringLiteral("horizontal")] = (i % 2 == 0);
            parent[QStringLiteral("first")] = current;
            parent[QStringLiteral("second")] = secondLeaf;
            current = parent;
        }

        QJsonObject wrapper;
        wrapper[QStringLiteral("root")] = current;

        auto tree = SplitTree::fromJson(wrapper);
        // nodeFromJson returns nullptr for children beyond the max depth,
        // which causes parent internal nodes to also return nullptr.
        // The tree should be rejected (nullptr) — must not stack overflow.
        QVERIFY(tree == nullptr);
    }

    // =========================================================================
    // Edge case tests
    // =========================================================================

    void testRemove_nonexistent()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));
        QCOMPARE(tree.leafCount(), 3);

        tree.remove(QStringLiteral("nonexistent"));
        QCOMPARE(tree.leafCount(), 3); // Should be unchanged
        QCOMPARE(tree.leafOrder(),
                 (QStringList{QStringLiteral("win1"), QStringLiteral("win2"), QStringLiteral("win3")}));
    }

    void testRebuildFromOrderPreservesRatios()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"), 0.5);
        tree.insertAtEnd(QStringLiteral("win2"), 0.5);
        tree.insertAtEnd(QStringLiteral("win3"), 0.5);
        // Customize a ratio
        tree.resizeSplit(QStringLiteral("win2"), 0.3);
        qreal originalRatio = tree.root()->splitRatio;

        // Rebuild with same order
        tree.rebuildFromOrder({QStringLiteral("win1"), QStringLiteral("win2"), QStringLiteral("win3")}, 0.5);

        // Ratio should be preserved
        QVERIFY(qFuzzyCompare(tree.root()->splitRatio, originalRatio));
    }

    void testSwap_withNonexistentWindow()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        auto orderBefore = tree.leafOrder();
        tree.swap(QStringLiteral("win1"), QStringLiteral("nonexistent"));
        auto orderAfter = tree.leafOrder();

        // Tree should be unchanged when one window ID doesn't exist
        QCOMPARE(orderAfter, orderBefore);
        QCOMPARE(tree.leafCount(), 3);
    }

    void testInsertAtPosition_outOfRange()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        // Negative index — should handle gracefully (falls back to insertAtEnd per docs)
        tree.insertAtPosition(QStringLiteral("win3"), -1);
        QCOMPARE(tree.leafCount(), 3);

        // Index beyond leaf count — should fall back to insertAtEnd
        tree.insertAtPosition(QStringLiteral("win4"), 999);
        QCOMPARE(tree.leafCount(), 4);

        // Verify tree is still consistent — all windows present
        auto order = tree.leafOrder();
        QCOMPARE(order.size(), 4);
        QVERIFY(order.contains(QStringLiteral("win1")));
        QVERIFY(order.contains(QStringLiteral("win2")));
        QVERIFY(order.contains(QStringLiteral("win3")));
        QVERIFY(order.contains(QStringLiteral("win4")));
    }

    void testResizeSplit_boundaryValues()
    {
        SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        // Ratio = 0.0 — should clamp to minimum
        tree.resizeSplit(QStringLiteral("win2"), 0.0);
        auto zones = tree.applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[1].width() >= 1);

        // Ratio = 1.0 — should clamp to maximum
        tree.resizeSplit(QStringLiteral("win2"), 1.0);
        zones = tree.applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[1].width() >= 1);

        // Negative ratio — should clamp
        tree.resizeSplit(QStringLiteral("win2"), -0.5);
        zones = tree.applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].width() >= 1);
        QVERIFY(zones[1].width() >= 1);
    }
};

QTEST_MAIN(TestSplitTree)
#include "test_split_tree.moc"
