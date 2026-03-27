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

void SplitTree::collectInternalNodeParams(const SplitNode* node, QVector<qreal>& ratios, QVector<bool>& directions)
{
    if (!node || node->isLeaf()) {
        return;
    }
    ratios.append(node->splitRatio);
    directions.append(node->splitHorizontal);
    collectInternalNodeParams(node->first.get(), ratios, directions);
    collectInternalNodeParams(node->second.get(), ratios, directions);
}

int SplitTree::applyInternalNodeParams(SplitNode* node, const QVector<qreal>& ratios, const QVector<bool>& directions,
                                       int index)
{
    if (!node || node->isLeaf()) {
        return index;
    }
    if (index < ratios.size() && index < directions.size()) {
        node->splitRatio = ratios[index];
        node->splitHorizontal = directions[index];
    }
    ++index;
    index = applyInternalNodeParams(node->first.get(), ratios, directions, index);
    index = applyInternalNodeParams(node->second.get(), ratios, directions, index);
    return index;
}

} // namespace PlasmaZones
