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

// Self-registration: BSP provides balanced recursive splitting (alphabetical priority 10)
namespace {
AlgorithmRegistrar<BSPAlgorithm> s_bspRegistrar(DBus::AutotileAlgorithm::BSP, 10);
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
    return i18n("Binary space partitioning - balanced recursive splitting");
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
    const auto &outerGaps = params.outerGaps;
    const auto &minSizes = params.minSizes;

    QVector<QRect> zones;

    if (windowCount <= 0 || !screenGeometry.isValid() || !params.state) {
        return zones;
    }

    const QRect area = innerRect(screenGeometry, outerGaps);

    // Single window takes full available area
    if (windowCount == 1) {
        zones.append(area);
        return zones;
    }

    // Read the user's split ratio from TilingState as the default for new nodes.
    const qreal defaultRatio = std::clamp(params.state->splitRatio(),
                                          MinSplitRatio, MaxSplitRatio);

    // Build a fresh tree from scratch each time for deterministic output.
    // Uses the actual screen area so split direction heuristics match
    // the real screen aspect ratio.
    std::unique_ptr<BSPNode> root;
    int leafCount = 0;
    buildTree(root, leafCount, windowCount, defaultRatio, area);

    // Apply geometry top-down with inner gaps at each split point.
    // minSizes are passed so BSP clamps split ratios at each node to
    // satisfy subtree minimum dimensions.
    applyGeometry(root.get(), area, innerGap, minSizes, 0);

    // Collect leaf geometries
    collectLeaves(root.get(), zones);

    // Validate that all zones have positive dimensions.
    // applyGeometry() returns early on degenerate splits, leaving child leaves
    // with default geometry from construction.
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
// Tree construction
// =============================================================================

void BSPAlgorithm::buildTree(std::unique_ptr<BSPNode> &root, int &leafCount,
                              int windowCount, qreal defaultRatio, const QRect &refRect)
{
    root.reset();
    leafCount = 0;

    if (windowCount <= 0) {
        return;
    }

    // Start with a single leaf as root
    root = std::make_unique<BSPNode>();
    leafCount = 1;

    // Use actual screen geometry so split direction heuristics match the
    // real screen. Falls back to 1920x1080 if the provided rect is invalid.
    const QRect buildRect = refRect.isValid() ? refRect : QRect(0, 0, 1920, 1080);

    // Grow one leaf at a time up to the target count
    int iterations = 0;
    constexpr int MaxIterations = 1000;
    while (leafCount < windowCount && iterations++ < MaxIterations) {
        // Apply geometry so largestLeaf can find the optimal split candidate
        applyGeometry(root.get(), buildRect, 0, {}, 0);
        if (!growTree(root.get(), leafCount, defaultRatio)) {
            break;
        }
    }
}

bool BSPAlgorithm::growTree(BSPNode *root, int &leafCount, qreal defaultRatio)
{
    if (!root) {
        return false;
    }

    // Find the largest leaf to split (produces balanced layouts)
    BSPNode *leaf = largestLeaf(root);
    if (!leaf || !leaf->isLeaf()) {
        return false;
    }

    // Split this leaf into an internal node with two leaf children
    leaf->first = std::make_unique<BSPNode>();
    leaf->first->parent = leaf;

    leaf->second = std::make_unique<BSPNode>();
    leaf->second->parent = leaf;

    // Choose split direction based on current geometry (if available) or default
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
    ++leafCount;
    return true;
}

// =============================================================================
// Geometry computation (top-down)
// =============================================================================

QSize BSPAlgorithm::computeSubtreeMinDims(const BSPNode *node, const QVector<QSize> &minSizes,
                                           int leafStartIdx, int innerGap, int &leafCount)
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
                                  const QVector<QSize> &minSizes, int leafStartIdx)
{
    if (!node) {
        return;
    }

    node->geometry = rect;

    if (node->isLeaf()) {
        return;
    }

    // Use the node's own split ratio (set when it was created or resized),
    // enabling per-split adjustment like bspwm.
    qreal ratio = std::clamp(node->splitRatio, MinSplitRatio, MaxSplitRatio);

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
                ratio = clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio,
                                                     firstMin.height(), secondMin.height());
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
                ratio = clampOrProportionalFallback(ratio, minFirstRatio, maxFirstRatio,
                                                     firstMin.width(), secondMin.width());
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

        applyGeometry(node->first.get(), firstRect, innerGap, minSizes, leafStartIdx);
        applyGeometry(node->second.get(), secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves);
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

        applyGeometry(node->first.get(), firstRect, innerGap, minSizes, leafStartIdx);
        applyGeometry(node->second.get(), secondRect, innerGap, minSizes, leafStartIdx + firstChildLeaves);
    }
}

void BSPAlgorithm::collectLeaves(const BSPNode *node, QVector<QRect> &zones)
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

bool BSPAlgorithm::chooseSplitDirection(const QRect &geometry)
{
    // Split perpendicular to longest axis for balanced regions
    return geometry.height() > geometry.width();
}

} // namespace PlasmaZones
