// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BSPAlgorithm.h"
#include "../AlgorithmRegistry.h"
#include "../TilingState.h"
#include "core/constants.h"
#include <KLocalizedString>
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

QString BSPAlgorithm::name() const
{
    return i18n("BSP");
}

QString BSPAlgorithm::description() const
{
    return i18n("Binary space partitioning - persistent tree layout");
}

QString BSPAlgorithm::icon() const noexcept
{
    return QStringLiteral("view-grid-symbolic");
}

QVector<QRect> BSPAlgorithm::calculateZones(const TilingParams &params) const
{
    const int windowCount = params.windowCount;
    const auto &screenGeometry = params.screenGeometry;
    const int innerGap = params.innerGap;
    const int outerGap = params.outerGap;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const auto &state = *params.state;

    const QRect area = innerRect(screenGeometry, outerGap);

    // Single window takes full available area — keep the tree intact so split
    // ratios are preserved when windows return
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    const qreal stateRatio = std::clamp(state.splitRatio(), MinSplitRatio, MaxSplitRatio);

    // Grow or shrink the persistent tree to match window count.
    // Uses the actual screen area (not hardcoded 1920x1080) so split
    // direction heuristics match the real screen aspect ratio.
    ensureTreeSize(windowCount, stateRatio, area);

    // Apply geometry top-down with inner gaps at each split point.
    // Passes stateRatio so ALL nodes use the current slider value
    // (overrides per-node ratios that were frozen at construction time).
    //
    // minSizes are passed so BSP clamps split ratios at each node to
    // satisfy subtree minimum dimensions. Per-node clamping at H-splits
    // may produce slightly different y-boundaries in sibling subtrees
    // (expected for BSP — each subtree is independent). The root V-split
    // uses a single aggregate clamp so the main vertical boundary stays
    // consistent. This is preferable to deferring to post-processing,
    // which can't correctly propagate boundary shifts across tree levels.
    applyGeometry(m_root.get(), area, innerGap, minSizes, 0, stateRatio);

    // Collect leaf geometries
    collectLeaves(m_root.get(), zones);

    // Validate that all zones have positive dimensions.
    // applyGeometry() returns early on degenerate splits, leaving child leaves
    // with stale/default geometry from construction.
    bool hasInvalidZone = false;
    for (const QRect &zone : zones) {
        if (!zone.isValid() || zone.width() <= 0 || zone.height() <= 0) {
            hasInvalidZone = true;
            break;
        }
    }
    if (hasInvalidZone) {
        // Fall back to gap-aware equal columns layout
        zones.clear();
        const QVector<int> columnWidths = distributeWithGaps(area.width(), windowCount, innerGap);
        int currentX = area.x();
        for (int i = 0; i < windowCount; ++i) {
            zones.append(QRect(currentX, area.y(), columnWidths[i], area.height()));
            currentX += columnWidths[i] + innerGap;
        }
    }

    return zones;
}

// =============================================================================
// Tree size management
// =============================================================================

void BSPAlgorithm::ensureTreeSize(int windowCount, qreal defaultRatio, const QRect &refRect) const
{
    // No tree yet or corrupted state — build from scratch
    if (!m_root || m_leafCount <= 0) {
        buildTree(windowCount, defaultRatio, refRect);
        return;
    }

    // Incremental: add or remove one leaf at a time (with iteration guard)
    const int maxIterations = windowCount + m_leafCount + 1;
    int iterations = 0;
    while (m_leafCount < windowCount && iterations++ < maxIterations) {
        if (!growTree(defaultRatio)) {
            // Grow failed (shouldn't happen), rebuild
            buildTree(windowCount, defaultRatio, refRect);
            return;
        }
    }

    iterations = 0;
    while (m_leafCount > windowCount && iterations++ < maxIterations) {
        if (!shrinkTree()) {
            // Shrink failed (shouldn't happen), rebuild
            buildTree(windowCount, defaultRatio, refRect);
            return;
        }
    }
}

void BSPAlgorithm::buildTree(int windowCount, qreal defaultRatio, const QRect &refRect) const
{
    m_root.reset();
    m_leafCount = 0;

    if (windowCount <= 0) {
        return;
    }

    // Start with a single leaf as root
    m_root = std::make_unique<BSPNode>();
    m_leafCount = 1;

    // Use actual screen geometry so split direction heuristics match the
    // real screen. Falls back to 1920x1080 if the provided rect is invalid.
    const QRect buildRect = refRect.isValid() ? refRect : QRect(0, 0, 1920, 1080);

    // Grow one leaf at a time up to the target count
    int iterations = 0;
    constexpr int MaxIterations = 1000;
    while (m_leafCount < windowCount && iterations++ < MaxIterations) {
        // Apply geometry so largestLeaf can find the optimal split candidate
        applyGeometry(m_root.get(), buildRect, 0, {}, 0, defaultRatio);
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

    // Find the largest leaf to split (produces balanced layouts)
    BSPNode *leaf = largestLeaf(m_root.get());
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

    // Find the deepest rightmost leaf to remove (preserves larger early splits)
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

QSize BSPAlgorithm::computeSubtreeMinDims(const BSPNode *node, const QVector<QSize> &minSizes,
                                           int leafStartIdx, int innerGap, int &leafCount) const
{
    if (!node) {
        leafCount = 0;
        return QSize(0, 0);
    }

    if (node->isLeaf()) {
        leafCount = 1;
        if (leafStartIdx < minSizes.size()) {
            const QSize &ms = minSizes[leafStartIdx];
            return QSize(std::max(ms.width(), 0), std::max(ms.height(), 0));
        }
        return QSize(0, 0);
    }

    int firstLeafCount = 0;
    int secondLeafCount = 0;
    QSize firstMin = computeSubtreeMinDims(node->first.get(), minSizes,
                                           leafStartIdx, innerGap, firstLeafCount);
    QSize secondMin = computeSubtreeMinDims(node->second.get(), minSizes,
                                            leafStartIdx + firstLeafCount, innerGap, secondLeafCount);
    leafCount = firstLeafCount + secondLeafCount;

    if (node->splitHorizontal) {
        // Top/bottom split: width = max, height = sum + gap
        return QSize(std::max(firstMin.width(), secondMin.width()),
                     firstMin.height() + innerGap + secondMin.height());
    } else {
        // Left/right split: width = sum + gap, height = max
        return QSize(firstMin.width() + innerGap + secondMin.width(),
                     std::max(firstMin.height(), secondMin.height()));
    }
}

void BSPAlgorithm::applyGeometry(BSPNode *node, const QRect &rect, int innerGap,
                                  const QVector<QSize> &minSizes, int leafStartIdx,
                                  qreal stateRatio) const
{
    if (!node) {
        return;
    }

    node->geometry = rect;

    if (node->isLeaf()) {
        return;
    }

    // Use state ratio for ALL nodes so the split ratio slider updates
    // all splits uniformly. Per-node ratios (set at construction) are
    // overridden to ensure consistent behavior when the user adjusts the slider.
    qreal ratio = std::clamp(stateRatio, MinSplitRatio, MaxSplitRatio);

    // Clamp ratio to respect subtree minimum dimensions
    if (!minSizes.isEmpty()) {
        int firstLeafCount = 0;
        int secondLeafCount = 0;
        QSize firstMin = computeSubtreeMinDims(node->first.get(), minSizes,
                                               leafStartIdx, innerGap, firstLeafCount);
        QSize secondMin = computeSubtreeMinDims(node->second.get(), minSizes,
                                                leafStartIdx + firstLeafCount, innerGap, secondLeafCount);

        if (node->splitHorizontal) {
            const int contentHeight = rect.height() - innerGap;
            if (contentHeight > 0 && (firstMin.height() > 0 || secondMin.height() > 0)) {
                qreal minFirstRatio = (firstMin.height() > 0)
                    ? static_cast<qreal>(firstMin.height()) / contentHeight : MinSplitRatio;
                qreal maxFirstRatio = (secondMin.height() > 0)
                    ? 1.0 - static_cast<qreal>(secondMin.height()) / contentHeight : MaxSplitRatio;
                minFirstRatio = std::clamp(minFirstRatio, MinSplitRatio, MaxSplitRatio);
                maxFirstRatio = std::clamp(maxFirstRatio, MinSplitRatio, MaxSplitRatio);
                if (minFirstRatio <= maxFirstRatio) {
                    ratio = std::clamp(ratio, minFirstRatio, maxFirstRatio);
                }
            }
        } else {
            const int contentWidth = rect.width() - innerGap;
            if (contentWidth > 0 && (firstMin.width() > 0 || secondMin.width() > 0)) {
                qreal minFirstRatio = (firstMin.width() > 0)
                    ? static_cast<qreal>(firstMin.width()) / contentWidth : MinSplitRatio;
                qreal maxFirstRatio = (secondMin.width() > 0)
                    ? 1.0 - static_cast<qreal>(secondMin.width()) / contentWidth : MaxSplitRatio;
                minFirstRatio = std::clamp(minFirstRatio, MinSplitRatio, MaxSplitRatio);
                maxFirstRatio = std::clamp(maxFirstRatio, MinSplitRatio, MaxSplitRatio);
                if (minFirstRatio <= maxFirstRatio) {
                    ratio = std::clamp(ratio, minFirstRatio, maxFirstRatio);
                }
            }
        }
    }

    // Count first child leaves for leaf index threading
    const int firstChildLeaves = countLeaves(node->first.get());

    if (node->splitHorizontal) {
        // Split top/bottom with innerGap between children
        const int contentHeight = rect.height() - innerGap;
        if (contentHeight <= 0) {
            return; // Gap exceeds available space — leaves retain parent geometry
        }
        const int firstHeight = static_cast<int>(contentHeight * ratio);
        const int secondHeight = contentHeight - firstHeight;
        const QRect firstRect(rect.x(), rect.y(), rect.width(), firstHeight);
        const QRect secondRect(rect.x(), rect.y() + firstHeight + innerGap, rect.width(), secondHeight);

        // Guard: skip split if either partition is degenerate
        if (!firstRect.isValid() || !secondRect.isValid()) {
            return;
        }

        applyGeometry(node->first.get(), firstRect, innerGap, minSizes, leafStartIdx, stateRatio);
        applyGeometry(node->second.get(), secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves, stateRatio);
    } else {
        // Split left/right with innerGap between children
        const int contentWidth = rect.width() - innerGap;
        if (contentWidth <= 0) {
            return; // Gap exceeds available space — leaves retain parent geometry
        }
        const int firstWidth = static_cast<int>(contentWidth * ratio);
        const int secondWidth = contentWidth - firstWidth;
        const QRect firstRect(rect.x(), rect.y(), firstWidth, rect.height());
        const QRect secondRect(rect.x() + firstWidth + innerGap, rect.y(), secondWidth, rect.height());

        // Guard: skip split if either partition is degenerate
        if (!firstRect.isValid() || !secondRect.isValid()) {
            return;
        }

        applyGeometry(node->first.get(), firstRect, innerGap, minSizes, leafStartIdx, stateRatio);
        applyGeometry(node->second.get(), secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves, stateRatio);
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

BSPAlgorithm::BSPNode *BSPAlgorithm::largestLeaf(BSPNode *node)
{
    if (!node) {
        return nullptr;
    }
    if (node->isLeaf()) {
        return node;
    }

    BSPNode *left = largestLeaf(node->first.get());
    BSPNode *right = largestLeaf(node->second.get());

    if (!left) return right;
    if (!right) return left;

    // Compare areas when geometries are available
    const qint64 leftArea = left->geometry.isValid()
        ? static_cast<qint64>(left->geometry.width()) * left->geometry.height()
        : 0;
    const qint64 rightArea = right->geometry.isValid()
        ? static_cast<qint64>(right->geometry.width()) * right->geometry.height()
        : 0;

    // Fallback to right (deepest) when no geometry is available
    if (leftArea == 0 && rightArea == 0) {
        return right;
    }

    return (leftArea >= rightArea) ? left : right;
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
    // subtree, so removing it preserves earlier layout structure
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
