// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BSPAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <cmath>

namespace PlasmaZones {

using namespace AutotileDefaults;

// Self-registration: BSP provides balanced recursive splitting (priority 30)
namespace {
AlgorithmRegistrar<BSPAlgorithm> s_bspRegistrar(DBus::AutotileAlgorithm::BSP, 30);
}

BSPAlgorithm::BSPAlgorithm(QObject *parent)
    : TilingAlgorithm(parent)
{
}

QString BSPAlgorithm::name() const noexcept
{
    return QStringLiteral("BSP");
}

QString BSPAlgorithm::description() const
{
    return tr("Binary space partitioning - persistent tree layout");
}

QString BSPAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-grid-symbolic");
}

QVector<QRect> BSPAlgorithm::calculateZones(int windowCount, const QRect &screenGeometry,
                                            const TilingState &state) const
{
    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid()) {
        return zones;
    }

    // Single window takes full screen — keep the tree intact so split
    // ratios are preserved when windows return
    if (windowCount == 1) {
        zones.append(screenGeometry);
        return zones;
    }

    const qreal defaultRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Grow or shrink the persistent tree to match window count
    ensureTreeSize(windowCount, defaultRatio);

    // Apply geometry top-down using each node's stored split direction and ratio
    applyGeometry(m_root.get(), screenGeometry);

    // Collect leaf geometries
    collectLeaves(m_root.get(), zones);

    return zones;
}

// =============================================================================
// Tree size management
// =============================================================================

void BSPAlgorithm::ensureTreeSize(int windowCount, qreal defaultRatio) const
{
    // No tree yet or corrupted state — build from scratch
    if (!m_root || m_leafCount <= 0) {
        buildTree(windowCount, defaultRatio);
        return;
    }

    // Incremental: add or remove one leaf at a time (with iteration guard)
    const int maxIterations = windowCount + m_leafCount + 1;
    int iterations = 0;
    while (m_leafCount < windowCount && iterations++ < maxIterations) {
        if (!growTree(defaultRatio)) {
            // Grow failed (shouldn't happen), rebuild
            buildTree(windowCount, defaultRatio);
            return;
        }
    }

    iterations = 0;
    while (m_leafCount > windowCount && iterations++ < maxIterations) {
        if (!shrinkTree()) {
            // Shrink failed (shouldn't happen), rebuild
            buildTree(windowCount, defaultRatio);
            return;
        }
    }
}

void BSPAlgorithm::buildTree(int windowCount, qreal defaultRatio) const
{
    m_root.reset();
    m_leafCount = 0;

    if (windowCount <= 0) {
        return;
    }

    // Start with a single leaf as root
    m_root = std::make_unique<BSPNode>();
    m_leafCount = 1;

    // Grow one leaf at a time up to the target count
    int iterations = 0;
    constexpr int MaxIterations = 1000;
    while (m_leafCount < windowCount && iterations++ < MaxIterations) {
        if (!growTree(defaultRatio)) {
            break;
        }
    }
}

bool BSPAlgorithm::growTree(qreal defaultRatio) const
{
    if (!m_root) {
        return false;
    }

    // Find the deepest rightmost leaf to split
    BSPNode *leaf = deepestLeaf(m_root.get());
    if (!leaf || !leaf->isLeaf()) {
        return false;
    }

    // Split this leaf into an internal node with two leaf children
    // The existing leaf becomes the first child; a new leaf becomes the second
    leaf->first = std::make_unique<BSPNode>();
    leaf->first->parent = leaf;

    leaf->second = std::make_unique<BSPNode>();
    leaf->second->parent = leaf;

    // Choose split direction based on current geometry (if available) or default
    // On first build, geometry hasn't been applied yet, so use a heuristic:
    // alternate between horizontal and vertical based on tree depth
    if (leaf->geometry.isValid()) {
        leaf->splitHorizontal = chooseSplitDirection(leaf->geometry);
    } else {
        // Estimate: count depth from root and alternate
        int depth = 0;
        for (BSPNode *p = leaf->parent; p; p = p->parent) {
            ++depth;
        }
        leaf->splitHorizontal = (depth % 2 != 0);
    }

    leaf->splitRatio = defaultRatio;
    ++m_leafCount;
    return true;
}

bool BSPAlgorithm::shrinkTree() const
{
    if (!m_root || m_leafCount <= 1) {
        return false;
    }

    // Find the deepest rightmost leaf to remove
    BSPNode *leaf = deepestLeaf(m_root.get());
    if (!leaf || !leaf->parent) {
        return false;
    }

    BSPNode *parent = leaf->parent;

    // Determine which child is the sibling (the one that stays)
    BSPNode *sibling = nullptr;
    bool leafIsFirst = (parent->first.get() == leaf);
    if (leafIsFirst) {
        sibling = parent->second.get();
    } else {
        sibling = parent->first.get();
    }

    if (!sibling) {
        return false;
    }

    // Promote sibling to take parent's place
    BSPNode *grandparent = parent->parent;

    // Take ownership of the sibling subtree
    std::unique_ptr<BSPNode> siblingOwned;
    if (leafIsFirst) {
        siblingOwned = std::move(parent->second);
    } else {
        siblingOwned = std::move(parent->first);
    }

    siblingOwned->parent = grandparent;

    if (!grandparent) {
        // Parent was root — sibling becomes new root
        m_root = std::move(siblingOwned);
    } else if (grandparent->first.get() == parent) {
        grandparent->first = std::move(siblingOwned);
    } else {
        grandparent->second = std::move(siblingOwned);
    }

    --m_leafCount;
    return true;
}

// =============================================================================
// Geometry computation (top-down)
// =============================================================================

void BSPAlgorithm::applyGeometry(BSPNode *node, const QRect &rect) const
{
    if (!node) {
        return;
    }

    node->geometry = rect;

    if (node->isLeaf()) {
        return;
    }

    const qreal ratio = std::clamp(node->splitRatio, MinSplitRatio, MaxSplitRatio);

    if (node->splitHorizontal) {
        // Split top/bottom
        const int splitPos = rect.y() + static_cast<int>(rect.height() * ratio);
        const QRect firstRect(rect.x(), rect.y(), rect.width(), splitPos - rect.y());
        const QRect secondRect(rect.x(), splitPos, rect.width(), rect.bottom() - splitPos + 1);

        // Guard: skip split if either partition is degenerate
        if (!firstRect.isValid() || !secondRect.isValid()) {
            return;
        }

        applyGeometry(node->first.get(), firstRect);
        applyGeometry(node->second.get(), secondRect);
    } else {
        // Split left/right
        const int splitPos = rect.x() + static_cast<int>(rect.width() * ratio);
        const QRect firstRect(rect.x(), rect.y(), splitPos - rect.x(), rect.height());
        const QRect secondRect(splitPos, rect.y(), rect.right() - splitPos + 1, rect.height());

        // Guard: skip split if either partition is degenerate
        if (!firstRect.isValid() || !secondRect.isValid()) {
            return;
        }

        applyGeometry(node->first.get(), firstRect);
        applyGeometry(node->second.get(), secondRect);
    }
}

void BSPAlgorithm::collectLeaves(const BSPNode *node, QVector<QRect> &zones) const
{
    if (!node) {
        return;
    }

    if (node->isLeaf()) {
        zones.append(node->geometry);
    } else {
        collectLeaves(node->first.get(), zones);
        collectLeaves(node->second.get(), zones);
    }
}

// =============================================================================
// Tree traversal helpers
// =============================================================================

int BSPAlgorithm::countLeaves(const BSPNode *node)
{
    if (!node) {
        return 0;
    }
    if (node->isLeaf()) {
        return 1;
    }
    return countLeaves(node->first.get()) + countLeaves(node->second.get());
}

BSPAlgorithm::BSPNode *BSPAlgorithm::deepestLeaf(BSPNode *node)
{
    if (!node) {
        return nullptr;
    }
    if (node->isLeaf()) {
        return node;
    }

    // Prefer the second (right/bottom) child — this is the most recently added
    // subtree, so removing/splitting it preserves earlier layout structure
    BSPNode *right = deepestLeaf(node->second.get());
    if (right) {
        return right;
    }
    return deepestLeaf(node->first.get());
}

bool BSPAlgorithm::chooseSplitDirection(const QRect &geometry)
{
    // Split perpendicular to longest axis for balanced regions
    return geometry.height() > geometry.width();
}

} // namespace PlasmaZones
