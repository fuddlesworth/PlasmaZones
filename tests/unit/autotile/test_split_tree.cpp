// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QRect>
#include <QVector>

#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include "core/types/constants.h"

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

    PhosphorTiles::TilingAlgorithm* dwindleMemory()
    {
        return m_scriptSetup.registry()->algorithm(QLatin1String("dwindle-memory"));
    }

private Q_SLOTS:

    void initTestCase()
    {
        QVERIFY(m_scriptSetup.init(QStringLiteral(P_SOURCE_DIR)));
        QVERIFY(dwindleMemory() != nullptr);
    }

    // =========================================================================
    // PhosphorTiles::SplitTree core tests
    // =========================================================================

    void testEmpty()
    {
        PhosphorTiles::SplitTree tree;
        QVERIFY(tree.isEmpty());
        QCOMPARE(tree.leafCount(), 0);
        QVERIFY(tree.root() == nullptr);
        QVERIFY(tree.leafOrder().isEmpty());

        auto zones = tree.applyGeometry(m_screenGeometry, 0);
        QVERIFY(zones.isEmpty());
    }

    // =========================================================================
    // Interactive-resize edge→split resolution (splitOwningEdge / resizeSplitNode)
    // =========================================================================

    void testSplitOwningEdgeTwoWindows()
    {
        using Edge = PhosphorTiles::SplitTree::Edge;
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("a"));
        tree.insertAtEnd(QStringLiteral("b"));

        PhosphorTiles::SplitNode* root = tree.root();
        QVERIFY(root && !root->isLeaf());
        QVERIFY(!root->splitHorizontal); // a|b is a vertical (left/right) split

        // The shared boundary: a's right edge and b's left edge both own the root.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("a"), Edge::Right) == root);
        QVERIFY(tree.splitOwningEdge(QStringLiteral("b"), Edge::Left) == root);
        // Outer edges are screen boundaries — no owning split.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("a"), Edge::Left) == nullptr);
        QVERIFY(tree.splitOwningEdge(QStringLiteral("b"), Edge::Right) == nullptr);
        // Orthogonal axis has no split at this level.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("a"), Edge::Top) == nullptr);
        QVERIFY(tree.splitOwningEdge(QStringLiteral("a"), Edge::Bottom) == nullptr);
        // Unknown window.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("zzz"), Edge::Right) == nullptr);
    }

    void testSplitOwningEdgeNested()
    {
        using Edge = PhosphorTiles::SplitTree::Edge;
        // insertAtEnd alternates split direction: root V(a, H(b, c)).
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("a"));
        tree.insertAtEnd(QStringLiteral("b"));
        tree.insertAtEnd(QStringLiteral("c"));

        PhosphorTiles::SplitNode* root = tree.root();
        QVERIFY(root && !root->splitHorizontal);
        PhosphorTiles::SplitNode* hnode = root->second.get();
        QVERIFY(hnode && hnode->splitHorizontal);

        // b/c are stacked inside root's right column. Their LEFT edge is the
        // root's vertical split (nearest collinear ancestor, skipping the
        // orthogonal H split), not the inner one.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("b"), Edge::Left) == root);
        QVERIFY(tree.splitOwningEdge(QStringLiteral("c"), Edge::Left) == root);
        // The b|c horizontal boundary is owned by the inner H node.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("b"), Edge::Bottom) == hnode);
        QVERIFY(tree.splitOwningEdge(QStringLiteral("c"), Edge::Top) == hnode);
        // a's right edge is the root split; c's right edge is the screen.
        QVERIFY(tree.splitOwningEdge(QStringLiteral("a"), Edge::Right) == root);
        QVERIFY(tree.splitOwningEdge(QStringLiteral("c"), Edge::Right) == nullptr);
    }

    void testResizeSplitNodeReflowsAndClamps()
    {
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("a"));
        tree.insertAtEnd(QStringLiteral("b"));
        PhosphorTiles::SplitNode* root = tree.root();

        tree.resizeSplitNode(root, 0.25);
        QCOMPARE(root->splitRatio, 0.25);
        const QVector<QRect> zones = tree.applyGeometry(QRect(0, 0, 1000, 1000), 0);
        QCOMPARE(zones.size(), 2);
        QVERIFY(zones[0].width() >= 240 && zones[0].width() <= 260); // a ≈ 25%
        QVERIFY(zones[1].width() >= 740); // b absorbed the rest

        // Out-of-range ratios clamp to [MinSplitRatio, MaxSplitRatio].
        tree.resizeSplitNode(root, 5.0);
        QVERIFY(root->splitRatio <= PhosphorTiles::AutotileDefaults::MaxSplitRatio + 1e-9);
        tree.resizeSplitNode(root, -1.0);
        QVERIFY(root->splitRatio >= PhosphorTiles::AutotileDefaults::MinSplitRatio - 1e-9);

        // Null and leaf nodes are no-ops (don't disturb the root ratio).
        const qreal before = root->splitRatio;
        tree.resizeSplitNode(nullptr, 0.5);
        tree.resizeSplitNode(tree.leafForWindow(QStringLiteral("a")), 0.5);
        QCOMPARE(root->splitRatio, before);
    }

    // A corner drag moves one horizontal-axis edge and one vertical-axis edge.
    // The two edges must resolve to two DISTINCT non-null splits (of opposite
    // orientation) so the engine can adjust each independently without one
    // clobbering the other. Tree: root V(a, H(b, c)); corner-resize c.
    void testSplitOwningEdgeCornerTwoDistinctSplits()
    {
        using Edge = PhosphorTiles::SplitTree::Edge;
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("a"));
        tree.insertAtEnd(QStringLiteral("b"));
        tree.insertAtEnd(QStringLiteral("c"));

        // c's top-left corner: Left edge (vertical split) + Top edge (horizontal split).
        const PhosphorTiles::SplitNode* leftOwner = tree.splitOwningEdge(QStringLiteral("c"), Edge::Left);
        const PhosphorTiles::SplitNode* topOwner = tree.splitOwningEdge(QStringLiteral("c"), Edge::Top);
        QVERIFY(leftOwner != nullptr);
        QVERIFY(topOwner != nullptr);
        QVERIFY(leftOwner != topOwner); // distinct nodes — no double-application
        QVERIFY(!leftOwner->splitHorizontal); // Left → vertical split (the root)
        QVERIFY(topOwner->splitHorizontal); // Top → horizontal split (the inner node)
    }

    void testInsertFirst()
    {
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        QCOMPARE(tree.leafCount(), 3);
        QCOMPARE(tree.leafOrder(),
                 (QStringList{QStringLiteral("win1"), QStringLiteral("win2"), QStringLiteral("win3")}));
    }

    void testInsertAtFocused()
    {
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.remove(QStringLiteral("win1"));

        QVERIFY(tree.isEmpty());
        QCOMPARE(tree.leafCount(), 0);
        QVERIFY(tree.root() == nullptr);
    }

    void testSwap()
    {
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        tree.resizeSplit(QStringLiteral("win2"), 0.3);

        auto* root = tree.root();
        QVERIFY(root != nullptr);
        QVERIFY(!root->isLeaf());
        QCOMPARE(root->splitRatio, 0.3);
    }

    void testSwapLeaves_bothPresent()
    {
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        const bool ok = tree.swapLeaves(QStringLiteral("win1"), QStringLiteral("win3"));
        QVERIFY(ok);

        auto order = tree.leafOrder();
        QCOMPARE(order.size(), 3);
        QCOMPARE(order[0], QStringLiteral("win3"));
        QCOMPARE(order[1], QStringLiteral("win2"));
        QCOMPARE(order[2], QStringLiteral("win1"));

        // Structure unchanged — tree still has 3 leaves and a non-leaf root.
        QCOMPARE(tree.leafCount(), 3);
        QVERIFY(!tree.root()->isLeaf());
    }

    void testSwapLeaves_eitherMissing()
    {
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        const auto orderBefore = tree.leafOrder();

        // First id missing
        QVERIFY(!tree.swapLeaves(QStringLiteral("nope"), QStringLiteral("win2")));
        QCOMPARE(tree.leafOrder(), orderBefore);

        // Second id missing
        QVERIFY(!tree.swapLeaves(QStringLiteral("win1"), QStringLiteral("nope")));
        QCOMPARE(tree.leafOrder(), orderBefore);

        // Both missing
        QVERIFY(!tree.swapLeaves(QStringLiteral("nope1"), QStringLiteral("nope2")));
        QCOMPARE(tree.leafOrder(), orderBefore);
    }

    void testSwapLeaves_preservesGeometry()
    {
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));
        tree.insertAtEnd(QStringLiteral("win3"));

        // Customise some ratios so we can verify they survive the swap.
        tree.resizeSplit(QStringLiteral("win2"), 0.7);
        const qreal rootRatioBefore = tree.root()->splitRatio;
        const bool rootHorizontalBefore = tree.root()->splitHorizontal;
        const auto zonesBefore = tree.applyGeometry(m_screenGeometry, 0);

        QVERIFY(tree.swapLeaves(QStringLiteral("win1"), QStringLiteral("win3")));

        // Split ratios / directions are untouched by the id-only swap.
        QVERIFY(qFuzzyCompare(tree.root()->splitRatio, rootRatioBefore));
        QCOMPARE(tree.root()->splitHorizontal, rootHorizontalBefore);

        // Because swapLeaves only exchanges ids on existing leaves, the per-slot
        // geometry the tree produces must be byte-identical to before.
        const auto zonesAfter = tree.applyGeometry(m_screenGeometry, 0);
        QCOMPARE(zonesAfter, zonesBefore);
    }

    void testSwapLeaves_selfSwap()
    {
        PhosphorTiles::SplitTree tree;
        tree.insertAtEnd(QStringLiteral("win1"));
        tree.insertAtEnd(QStringLiteral("win2"));

        // Self-swap on an existing id is a successful no-op.
        QVERIFY(tree.swapLeaves(QStringLiteral("win1"), QStringLiteral("win1")));
        QCOMPARE(tree.leafOrder(), (QStringList{QStringLiteral("win1"), QStringLiteral("win2")}));

        // Self-swap on a missing id still reports failure.
        QVERIFY(!tree.swapLeaves(QStringLiteral("nope"), QStringLiteral("nope")));
    }

    // =========================================================================
    // Geometry tests
    // =========================================================================

    void testApplyGeometry_twoWindows()
    {
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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

    // =========================================================================
    // Lookup tests
    // =========================================================================

    void testLeafForWindow()
    {
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        auto tree = std::make_unique<PhosphorTiles::SplitTree>();
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // Tree has only 2 leaves but state has 3 windows — should fall back
        auto tree = std::make_unique<PhosphorTiles::SplitTree>();
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));
        state.addWindow(QStringLiteral("win3"));

        // PhosphorTiles::TilingState::addWindow triggers syncTreeLazyCreate, so tree exists
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));
        state.addWindow(QStringLiteral("win2"));

        // Manually set a tree with custom ratio
        auto tree = std::make_unique<PhosphorTiles::SplitTree>();
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
        PhosphorTiles::TilingState state(QStringLiteral("test"));
        state.addWindow(QStringLiteral("win1"));

        algo->prepareTilingState(&state);
        QVERIFY(state.splitTree() == nullptr);
    }

    // =========================================================================
    // Corrupted JSON deserialization tests
    // =========================================================================

    // =========================================================================
    // Edge case tests
    // =========================================================================

    void testRemove_nonexistent()
    {
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
        PhosphorTiles::SplitTree tree;
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
