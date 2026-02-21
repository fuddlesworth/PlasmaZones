// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../TilingAlgorithm.h"
#include <memory>

namespace PlasmaZones {

/**
 * @brief Binary Space Partitioning tiling algorithm with persistent tree
 *
 * Maintains a persistent binary tree that survives across retile operations,
 * matching the behavior of bspwm and Hyprland's dwindle layout. Each internal
 * node stores its own split direction and ratio, allowing:
 *
 * - Stable window positions when other windows are added/removed
 * - Per-split ratio adjustment (each border can be resized independently)
 * - Predictable insertion (new windows split the most recent leaf)
 *
 * When a window is added, the deepest (most recent) leaf splits into an
 * internal node with two children. When a window is removed, the removed
 * leaf's sibling is promoted to replace their parent node. In both cases,
 * the rest of the tree is untouched, preserving all existing split
 * directions and ratios.
 *
 * Layout example (5 windows, added sequentially):
 * ```
 * +-------------+-------------+
 * |             |             |
 * |      1      |      2      |
 * |             |             |
 * +-------------+------+------+
 * |             |      |      |
 * |      3      |  4   |  5   |
 * |             |      |      |
 * +-------------+------+------+
 * ```
 */
class PLASMAZONES_EXPORT BSPAlgorithm : public TilingAlgorithm
{
    Q_OBJECT

public:
    explicit BSPAlgorithm(QObject *parent = nullptr);
    ~BSPAlgorithm() override = default;

    // TilingAlgorithm interface
    QString name() const override;
    QString description() const override;
    QString icon() const noexcept override;

    QVector<QRect> calculateZones(const TilingParams &params) const override;

    bool supportsMasterCount() const noexcept override { return false; }
    bool supportsSplitRatio() const noexcept override { return true; }
    qreal defaultSplitRatio() const noexcept override { return 0.5; }
    int defaultMaxWindows() const noexcept override { return 5; }

private:
    /**
     * @brief Persistent BSP tree node
     *
     * Internal nodes have two children and define a split direction + ratio.
     * Leaf nodes represent individual windows and have no children.
     */
    struct BSPNode {
        qreal splitRatio = 0.5;       ///< How to divide this node's space
        bool splitHorizontal = false;  ///< true = top/bottom, false = left/right
        QRect geometry;                ///< Computed geometry (set during layout pass)
        std::unique_ptr<BSPNode> first;
        std::unique_ptr<BSPNode> second;
        BSPNode *parent = nullptr;     ///< Non-owning back-pointer

        bool isLeaf() const { return !first && !second; }
    };

    /**
     * @brief Ensure the tree has exactly the right number of leaves
     *
     * Grows or shrinks the tree incrementally to match windowCount.
     * Single-step changes (the common case) only modify one node.
     * Large jumps rebuild the tree from scratch.
     *
     * @param refRect Screen geometry for split direction heuristics during build
     */
    void ensureTreeSize(int windowCount, qreal defaultRatio, const QRect &refRect) const;

    /**
     * @brief Build a balanced tree from scratch for N windows
     *
     * @param refRect Screen geometry for split direction heuristics
     */
    void buildTree(int windowCount, qreal defaultRatio, const QRect &refRect) const;

    /**
     * @brief Split the deepest leaf to add one more window slot
     * @return true if a leaf was split
     */
    bool growTree(qreal defaultRatio) const;

    /**
     * @brief Remove the deepest leaf, promoting its sibling
     * @return true if a leaf was removed
     */
    bool shrinkTree() const;

    /**
     * @brief Apply geometry to all nodes top-down from root
     *
     * Recursively computes child geometries from parent geometry.
     * Uses stateRatio for ALL nodes (overriding per-node ratios) so
     * the split ratio slider updates all splits uniformly.
     * When minSizes is non-empty, clamps the ratio so both subtrees
     * get at least their minimum dimension.
     *
     * @param stateRatio Split ratio from TilingState (user-adjustable)
     * @param leafStartIdx Index of the first leaf in minSizes for this subtree
     */
    void applyGeometry(BSPNode *node, const QRect &rect, int innerGap,
                       const QVector<QSize> &minSizes, int leafStartIdx,
                       qreal stateRatio) const;

    /**
     * @brief Compute minimum width and height required by a subtree
     *
     * Aggregates leaf min sizes along split directions, adding gaps.
     * For a leaf, returns the min size from minSizes[leafStartIdx].
     * For an internal node, combines children based on split direction.
     *
     * @param[out] leafCount Number of leaves in this subtree
     * @return Minimum QSize required by the subtree
     */
    QSize computeSubtreeMinDims(const BSPNode *node, const QVector<QSize> &minSizes,
                                int leafStartIdx, int innerGap, int &leafCount) const;

    /**
     * @brief Collect leaf geometries in tree order (left-to-right, top-to-bottom)
     */
    void collectLeaves(const BSPNode *node, QVector<QRect> &zones) const;

    /**
     * @brief Count the number of leaf nodes in a subtree
     */
    static int countLeaves(const BSPNode *node);

    /**
     * @brief Find the leaf with the largest area (best candidate to split)
     *
     * Splitting the largest leaf produces the most balanced layouts,
     * matching how BSP is typically used in tiling window managers.
     * Falls back to deepest-rightmost when geometries haven't been
     * assigned yet (first build pass).
     */
    static BSPNode *largestLeaf(BSPNode *node);

    /**
     * @brief Find the deepest rightmost leaf (most recently added)
     *
     * Used by shrinkTree to remove the last-added leaf, preserving
     * the larger, earlier subdivisions.
     */
    static BSPNode *deepestLeaf(BSPNode *node);

    /**
     * @brief Choose split direction for a node based on its geometry
     *
     * Splits perpendicular to the longest axis (standard BSP heuristic).
     */
    static bool chooseSplitDirection(const QRect &geometry);

    // Persistent tree state (mutable for const calculateZones interface).
    // NOTE: Thread safety — these mutable members are mutated inside
    // calculateZones(). BSPAlgorithm is therefore NOT safe for concurrent
    // calls to calculateZones() on the same instance, unlike stateless
    // algorithms. The engine calls algorithms from a single thread so
    // this is safe in practice.
    mutable std::unique_ptr<BSPNode> m_root;
    mutable int m_leafCount = 0;
};

} // namespace PlasmaZones
