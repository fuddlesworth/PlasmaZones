// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphortiles_export.h>

#include "AutotileConstants.h"

#include <QJsonObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace PhosphorTiles {

/**
 * @brief A single node in the binary split tree
 *
 * Internal nodes have two children and define a split direction + ratio.
 * Leaf nodes represent individual windows and have no children.
 */
struct PHOSPHORTILES_EXPORT SplitNode
{
    qreal splitRatio = AutotileDefaults::DefaultSplitRatio; ///< How to divide this node's space (first child fraction)
    bool splitHorizontal = false; ///< true = top/bottom, false = left/right
    std::unique_ptr<SplitNode> first; ///< First child (left or top)
    std::unique_ptr<SplitNode> second; ///< Second child (right or bottom)
    SplitNode* parent = nullptr; ///< Non-owning back-pointer
    QString windowId; ///< Non-empty only for leaf nodes

    bool isLeaf() const
    {
        return !first && !second;
    }
};

/**
 * @brief A binary split tree for interactive window tiling
 *
 * SplitTree manages a binary tree of split nodes where each leaf represents
 * a tiled window and each internal node defines how its area is divided
 * between two children. This enables per-split ratio adjustment and
 * interactive insert/remove operations (similar to bspwm).
 *
 * Unlike BSPAlgorithm (which rebuilds the tree from scratch each frame),
 * SplitTree is a persistent data structure that is mutated incrementally
 * as windows are added, removed, swapped, or resized.
 *
 * This class is NOT a QObject. It is movable via unique_ptr members.
 *
 * @warning This class is not thread-safe. All access must be serialized by the caller.
 */
class PHOSPHORTILES_EXPORT SplitTree
{
public:
    /// Construct an empty tree (no root)
    SplitTree();

    /// Move constructor
    SplitTree(SplitTree&& other) noexcept;

    /// Move assignment
    SplitTree& operator=(SplitTree&& other) noexcept;

    ~SplitTree();

    // Non-copyable (unique_ptr members)
    SplitTree(const SplitTree&) = delete;
    SplitTree& operator=(const SplitTree&) = delete;

    // ═══════════════════════════════════════════════════════════════════════
    // Queries
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Get the root node of the tree
     * @return Root node, or nullptr if tree is empty
     */
    const SplitNode* root() const noexcept;
    SplitNode* root() noexcept;

    /**
     * @brief Check if the tree has no nodes
     */
    bool isEmpty() const noexcept;

    /**
     * @brief Count the number of leaf nodes (windows)
     */
    int leafCount() const noexcept;

    /**
     * @brief Get the maximum height of the tree
     * @return Height (0 for empty tree, 1 for single leaf)
     */
    int treeHeight() const noexcept;

    /**
     * @brief Find the leaf node for a given window ID
     * @param windowId Window to search for
     * @return Leaf node, or nullptr if not found
     */
    const SplitNode* leafForWindow(const QString& windowId) const;
    SplitNode* leafForWindow(const QString& windowId);

    /**
     * @brief Get window IDs in depth-first left-to-right order
     * @return Ordered list of window IDs from all leaf nodes
     */
    QStringList leafOrder() const;

    // ═══════════════════════════════════════════════════════════════════════
    // Mutations
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Insert a window by splitting the focused window's leaf
     *
     * If the tree is empty, creates a root leaf. If the focused window
     * is not found, falls back to insertAtEnd.
     *
     * @param windowId New window to insert
     * @param focusedWindowId Currently focused window to split
     */
    void insertAtFocused(const QString& windowId, const QString& focusedWindowId, qreal initialRatio = 0.0);

    /**
     * @brief Insert a window by splitting the rightmost leaf
     *
     * If the tree is empty, creates a root leaf.
     *
     * @param windowId New window to insert
     * @param initialRatio Split ratio for the new split (0 = use DefaultSplitRatio)
     */
    void insertAtEnd(const QString& windowId, qreal initialRatio = 0.0);

    /**
     * @brief Insert a window by splitting the leaf at a given position
     *
     * Position is a depth-first index. If position >= leafCount, falls
     * back to insertAtEnd.
     *
     * @param windowId New window to insert
     * @param position Depth-first leaf index to split
     * @param initialRatio Split ratio for the new split (0 = use DefaultSplitRatio)
     */
    void insertAtPosition(const QString& windowId, int position, qreal initialRatio = 0.0);

    /**
     * @brief Remove a window, collapsing its parent and promoting its sibling
     * @param windowId Window to remove
     */
    void remove(const QString& windowId);

    /**
     * @brief Swap two windows' positions in the tree
     *
     * Only swaps the windowId strings on the leaf nodes; tree structure
     * remains unchanged.
     *
     * @param windowId1 First window
     * @param windowId2 Second window
     */
    void swap(const QString& windowId1, const QString& windowId2);

    /**
     * @brief Swap the stored window ids on two existing leaves
     *
     * Fast-path variant of @ref swap that reports whether both leaves were
     * actually found. Callers can use the return value to decide whether the
     * swap succeeded or whether a fallback rebuild is required.
     *
     * Tree structure, split ratios, split directions, and child ordering are
     * left untouched; only the @c windowId strings on the two leaf nodes are
     * exchanged.
     *
     * @param a First window id
     * @param b Second window id
     * @return @c true if both leaves were located and their ids swapped;
     *         @c false otherwise (tree is left unchanged in the false case).
     *         Swapping an id with itself returns @c true as a no-op when the
     *         leaf exists, and @c false when it doesn't.
     */
    bool swapLeaves(const QString& a, const QString& b);

    /**
     * @brief Adjust the split ratio of a window's parent node
     * @param windowId Window whose parent split to resize
     * @param newRatio New split ratio (clamped to MinSplitRatio..MaxSplitRatio)
     */
    void resizeSplit(const QString& windowId, qreal newRatio);

    // ═══════════════════════════════════════════════════════════════════════
    // Geometry
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Compute zone rectangles by recursively splitting the area
     * @param area Available screen area
     * @param innerGap Gap between adjacent zones in pixels
     * @return Vector of zone geometries in depth-first left-to-right order
     *
     * @note When gap exceeds available space in a split, the `first` subtree receives
     * the full parent rect and each leaf of the `second` subtree is emitted as a
     * 1×1 rect anchored at the parent's origin. This preserves the invariant that
     * the returned rect count equals @ref leafCount, which downstream code relies
     * on to map tiled windows to zones by position. Windows under the degenerate
     * second subtree land effectively offscreen; this is intentional.
     * A warning is logged via lcTilesLib.
     */
    QVector<QRect> applyGeometry(const QRect& area, int innerGap) const;

    /**
     * @brief Rebuild tree from a new window order, preserving split ratios where possible
     * @param tiledWindows Ordered list of tiled window IDs
     * @param defaultSplitRatio Default split ratio for new internal nodes
     * @return @c true if all windows were preserved, @c false if the input had to
     *         be truncated (size exceeded @c MaxRuntimeTreeDepth). Callers that
     *         care about lossless round-tripping should observe the return value
     *         and clamp their own state to match.
     */
    bool rebuildFromOrder(const QStringList& tiledWindows,
                          qreal defaultSplitRatio = AutotileDefaults::DefaultSplitRatio);

    // ═══════════════════════════════════════════════════════════════════════
    // Serialization
    // ═══════════════════════════════════════════════════════════════════════

    /**
     * @brief Serialize the tree to JSON
     */
    QJsonObject toJson() const;

    /**
     * @brief Deserialize a tree from JSON
     * @param json Serialized tree
     * @return Reconstructed tree, or nullptr on error
     */
    static std::unique_ptr<SplitTree> fromJson(const QJsonObject& json);

private:
    enum class InsertReady {
        Proceed,
        Done,
        Rejected
    };
    /// Maximum runtime tree depth for insert operations and recursion guards.
    /// Uses the global constant from AutotileDefaults so that C++ SplitTree,
    /// JS applyTreeGeometry, and serialization all agree on the depth limit.
    static constexpr int MaxRuntimeTreeDepth = AutotileDefaults::MaxRuntimeTreeDepth;

    InsertReady prepareInsert(const QString& windowId);

    /// Insert at rightmost leaf without duplicate checking (for use by rebuildFromOrder)
    void insertAtEndRaw(const QString& windowId, qreal initialRatio);

    /// Insert at rightmost leaf, skipping prepareInsert (caller already called it)
    void insertAtEndImpl(const QString& windowId, qreal initialRatio);

    std::unique_ptr<SplitNode> m_root;

    SplitNode* findLeaf(SplitNode* node, const QString& windowId, int depth = 0) const;
    const SplitNode* findLeaf(const SplitNode* node, const QString& windowId, int depth = 0) const;
    SplitNode* leafAtIndex(SplitNode* node, int targetIndex, int& currentIndex, int depth = 0) const;
    const SplitNode* leafAtIndex(const SplitNode* node, int targetIndex, int& currentIndex, int depth = 0) const;
    SplitNode* rightmostLeaf(SplitNode* node) const;
    const SplitNode* rightmostLeaf(const SplitNode* node) const;
    void collectLeafOrder(const SplitNode* node, QStringList& order, int depth = 0) const;
    int countLeaves(const SplitNode* node, int depth = 0) const;
    void applyGeometryRecursive(const SplitNode* node, const QRect& rect, int innerGap, QVector<QRect>& zones,
                                int depth = 0) const;

    static int subtreeHeight(const SplitNode* node, int depth = 0);
    static void splitLeaf(SplitNode* leaf, const QString& newId, qreal ratio);

    static constexpr int MaxDeserializationDepth = AutotileDefaults::MaxRuntimeTreeDepth;
    static constexpr int MaxDeserializationNodes = 1024; ///< Limit total nodes to prevent memory exhaustion

    static QJsonObject nodeToJson(const SplitNode* node, int depth = 0);
    static std::unique_ptr<SplitNode> nodeFromJson(const QJsonObject& json, SplitNode* parent, int depth,
                                                   int& nodeCount, QSet<QString>& seenIds);
};

} // namespace PhosphorTiles
