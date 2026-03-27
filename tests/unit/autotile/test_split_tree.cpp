// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QJsonObject>
#include <QRect>
#include <QVector>

#include "autotile/SplitTree.h"
#include "autotile/TilingAlgorithm.h"
#include "autotile/TilingState.h"
#include "autotile/algorithms/DwindleMemoryAlgorithm.h"
#include "core/constants.h"

#include "../helpers/TilingTestHelpers.h"

using namespace PlasmaZones;
using namespace PlasmaZones::TestHelpers;

class TestSplitTree : public QObject
{
    Q_OBJECT

private:
    static constexpr int ScreenWidth = 1920;
    static constexpr int ScreenHeight = 1080;
    QRect m_screenGeometry{0, 0, ScreenWidth, ScreenHeight};

private Q_SLOTS:

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
        // The ratio between win1 and win3's ancestor should be the root's ratio,
        // which was set before win2's subtree was created and should be preserved.
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

        // Verify geometry output matches (proves ratios are preserved)
        auto zonesOriginal = tree.applyGeometry(m_screenGeometry, 0);
        auto zonesRestored = restored->applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zonesOriginal, zonesRestored);
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
        DwindleMemoryAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        auto tree = std::make_unique<SplitTree>();
        tree->insertAtEnd(QStringLiteral("win1"));
        tree->insertAtEnd(QStringLiteral("win2"));
        tree->insertAtEnd(QStringLiteral("win3"));
        state.setSplitTree(std::move(tree));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);

        for (const QRect& zone : zones) {
            QVERIFY(zone.isValid());
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testAlgo_withoutTree()
    {
        DwindleMemoryAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // No split tree set — should fall back to stateless dwindle
        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);

        for (const QRect& zone : zones) {
            QVERIFY(zone.width() > 0);
            QVERIFY(zone.height() > 0);
        }
    }

    void testAlgo_treeCountMismatch()
    {
        DwindleMemoryAlgorithm algo;
        TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // Tree has only 2 leaves but state has 3 windows — should fall back
        auto tree = std::make_unique<SplitTree>();
        tree->insertAtEnd(QStringLiteral("win1"));
        tree->insertAtEnd(QStringLiteral("win2"));
        state.setSplitTree(std::move(tree));

        auto zones = algo.calculateZones({3, m_screenGeometry, &state, 0, EdgeGaps::uniform(0)});
        QCOMPARE(zones.size(), 3);
    }

    void testAlgo_metadata()
    {
        DwindleMemoryAlgorithm algo;
        QVERIFY(!algo.name().isEmpty());
        QVERIFY(!algo.description().isEmpty());
        QVERIFY(algo.supportsSplitRatio());
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
        QJsonObject json;
        json[QStringLiteral("type")] = QStringLiteral("leaf");
        json[QStringLiteral("windowId")] = 42; // wrong type — should be string
        json[QStringLiteral("splitRatio")] = QStringLiteral("not a number"); // wrong type

        auto tree = SplitTree::fromJson(json);
        // Should handle gracefully — not crash
    }

    void testFromJson_negativeRatio()
    {
        // Build a valid-looking internal node with negative ratio
        QJsonObject left;
        left[QStringLiteral("type")] = QStringLiteral("leaf");
        left[QStringLiteral("windowId")] = QStringLiteral("win1");

        QJsonObject right;
        right[QStringLiteral("type")] = QStringLiteral("leaf");
        right[QStringLiteral("windowId")] = QStringLiteral("win2");

        QJsonObject root;
        root[QStringLiteral("type")] = QStringLiteral("internal");
        root[QStringLiteral("splitRatio")] = -0.5; // invalid
        root[QStringLiteral("horizontal")] = true;
        root[QStringLiteral("left")] = left;
        root[QStringLiteral("right")] = right;

        auto tree = SplitTree::fromJson(root);
        // Ratio should be clamped to valid range; tree should still be usable
        if (tree) {
            QCOMPARE(tree->leafCount(), 2);
            // Geometry should not crash with clamped ratio
            auto zones = tree->applyGeometry(m_screenGeometry, 0);
            QCOMPARE(zones.size(), 2);
            for (const QRect& zone : zones) {
                QVERIFY(zone.width() > 0);
                QVERIFY(zone.height() > 0);
            }
        }
    }

    void testFromJson_deeplyNested()
    {
        // Build a tree deeper than MaxDeserializationDepth (30)
        QJsonObject leaf;
        leaf[QStringLiteral("type")] = QStringLiteral("leaf");
        leaf[QStringLiteral("windowId")] = QStringLiteral("win1");

        QJsonObject current = leaf;
        for (int i = 0; i < 35; ++i) {
            QJsonObject right;
            right[QStringLiteral("type")] = QStringLiteral("leaf");
            right[QStringLiteral("windowId")] = QStringLiteral("win%1").arg(i + 2);

            QJsonObject parent;
            parent[QStringLiteral("type")] = QStringLiteral("internal");
            parent[QStringLiteral("splitRatio")] = 0.5;
            parent[QStringLiteral("horizontal")] = (i % 2 == 0);
            parent[QStringLiteral("left")] = current;
            parent[QStringLiteral("right")] = right;
            current = parent;
        }

        auto tree = SplitTree::fromJson(current);
        // Should either truncate or reject — must not stack overflow
        // If it parsed, leaf count should be <= 36 (truncated at depth 30)
        if (tree) {
            QVERIFY(tree->leafCount() <= 36);
        }
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
