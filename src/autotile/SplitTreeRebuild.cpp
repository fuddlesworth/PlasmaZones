// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"

#include <QSet>

namespace PlasmaZones {

using namespace AutotileDefaults;

// =============================================================================
// Rebuild from order (SOLID-1: moved from TilingState)
// =============================================================================

/**
 * @brief Rebuild the split tree from a new window order, preserving split ratios where possible
 *
 * Builds a fresh tree by inserting windows in the given order, then restores the old tree's
 * split ratios and directions if the leaf count has not changed.
 *
 * @note Ratio restoration only works correctly when the new tree has the same shape as the
 * old tree. Because insertAtEndRaw always builds a right-leaning chain, two trees with the
 * same leaf count will have identical shapes. However, if windows are reordered while count
 * stays the same, the ratios from the old tree are applied positionally (pre-order traversal)
 * to the new tree's internal nodes. This means each old ratio is applied to the structurally
 * corresponding node, which may govern a different pair of windows than it did before.
 *
 * @warning When window order changes but count stays the same, a ratio that previously
 * controlled (e.g.) windows A|B may now control windows C|D. The split positions are
 * preserved structurally, but their semantic meaning relative to specific windows is lost.
 *
 * @param tiledWindows Ordered list of tiled window IDs (duplicates and empties are filtered)
 * @param defaultSplitRatio Default split ratio for new internal nodes
 */
void SplitTree::rebuildFromOrder(const QStringList& tiledWindows, qreal defaultSplitRatio)
{
    if (tiledWindows.isEmpty()) {
        m_root.reset();
        return;
    }
    // Deduplicate input while preserving order, skipping empty IDs
    QStringList uniqueWindows;
    QSet<QString> seen;
    for (const auto& wid : tiledWindows) {
        if (!wid.isEmpty() && !seen.contains(wid)) {
            seen.insert(wid);
            uniqueWindows.append(wid);
        }
    }

    if (uniqueWindows.isEmpty()) {
        m_root.reset();
        return;
    }

    // Cap to prevent degenerate trees exceeding MaxRuntimeTreeDepth
    if (uniqueWindows.size() > MaxRuntimeTreeDepth + 1) {
        qCWarning(lcAutotile) << "rebuildFromOrder: truncating" << uniqueWindows.size() << "windows to"
                              << (MaxRuntimeTreeDepth + 1);
        uniqueWindows.resize(MaxRuntimeTreeDepth + 1);
    }

    if (uniqueWindows.size() == 1) {
        auto singleTree = std::make_unique<SplitNode>();
        singleTree->windowId = uniqueWindows.first();
        m_root = std::move(singleTree);
        return;
    }

    // Capture existing split ratios from old tree
    const int oldLeafCount = countLeaves(m_root.get());
    QVector<qreal> oldRatios;
    QVector<bool> oldDirections;
    collectInternalNodeParams(m_root.get(), oldRatios, oldDirections);

    // Build a fresh tree from deduplicated input (skip duplicate checks — already deduplicated)
    m_root.reset();
    for (const QString& windowId : uniqueWindows) {
        insertAtEndRaw(windowId, defaultSplitRatio);
    }

    // Restore ratios if leaf count matches
    if (oldLeafCount != leafCount()) {
        qCDebug(lcAutotile) << "rebuildFromOrder: leaf count changed from" << oldLeafCount << "to" << leafCount()
                            << "-- skipping ratio restoration";
    } else {
        applyInternalNodeParams(m_root.get(), oldRatios, oldDirections, 0);
    }
}

void SplitTree::collectInternalNodeParams(const SplitNode* node, QVector<qreal>& ratios, QVector<bool>& directions,
                                          int depth)
{
    if (!node || node->isLeaf() || depth >= MaxRuntimeTreeDepth) {
        return;
    }
    ratios.append(node->splitRatio);
    directions.append(node->splitHorizontal);
    collectInternalNodeParams(node->first.get(), ratios, directions, depth + 1);
    collectInternalNodeParams(node->second.get(), ratios, directions, depth + 1);
}

int SplitTree::applyInternalNodeParams(SplitNode* node, const QVector<qreal>& ratios, const QVector<bool>& directions,
                                       int index, int depth)
{
    if (!node || node->isLeaf() || depth >= MaxRuntimeTreeDepth) {
        return index;
    }
    if (index < ratios.size() && index < directions.size()) {
        node->splitRatio = ratios[index];
        node->splitHorizontal = directions[index];
    }
    ++index;
    index = applyInternalNodeParams(node->first.get(), ratios, directions, index, depth + 1);
    index = applyInternalNodeParams(node->second.get(), ratios, directions, index, depth + 1);
    return index;
}

} // namespace PlasmaZones
