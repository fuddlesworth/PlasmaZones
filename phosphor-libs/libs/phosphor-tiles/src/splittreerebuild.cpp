// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/SplitTree.h>
#include <PhosphorTiles/AutotileConstants.h>
#include "tileslogging.h"

#include <QHash>
#include <QSet>

namespace PhosphorTiles {

using namespace AutotileDefaults;

// =============================================================================
// Rebuild from order (moved from TilingState)
// =============================================================================

namespace {

/// Per-leaf record of the split that governed that leaf in the old tree.
struct LeafRatioRecord
{
    qreal ratio = AutotileDefaults::DefaultSplitRatio;
    bool horizontal = false;
    /// The windowId of the leaf's sibling in the old tree — so we only
    /// restore the ratio when the new tree's matching split has the same
    /// two leaves as children.
    QString siblingId;
    /// True when the sibling was itself a leaf in the old tree. When false,
    /// the old split governed a multi-leaf subtree and the ratio has no
    /// single-leaf meaning in the new tree — we fall back to default.
    bool siblingIsLeaf = false;
};

/// Walk @p node and record, for every leaf, the ratio/direction of the split
/// that its parent governed in the old tree.
void collectLeafRatios(const SplitNode* node, QHash<QString, LeafRatioRecord>& out, int depth = 0)
{
    if (!node || depth >= AutotileDefaults::MaxRuntimeTreeDepth) {
        return;
    }
    if (node->isLeaf()) {
        if (!node->windowId.isEmpty() && node->parent) {
            const SplitNode* parent = node->parent;
            const SplitNode* sibling = (parent->first.get() == node) ? parent->second.get() : parent->first.get();
            LeafRatioRecord rec;
            rec.ratio = parent->splitRatio;
            rec.horizontal = parent->splitHorizontal;
            if (sibling) {
                rec.siblingIsLeaf = sibling->isLeaf();
                if (rec.siblingIsLeaf) {
                    rec.siblingId = sibling->windowId;
                }
            }
            out.insert(node->windowId, rec);
        }
        return;
    }
    collectLeafRatios(node->first.get(), out, depth + 1);
    collectLeafRatios(node->second.get(), out, depth + 1);
}

/// For every internal node in the new tree, if BOTH its children are leaves
/// AND we recorded a ratio in the old tree keyed by one of them whose sibling
/// matches the other child's id, restore that ratio and direction. All other
/// internal nodes keep the defaults set by insertAtEndRaw.
void restoreLeafRatios(SplitNode* node, const QHash<QString, LeafRatioRecord>& recorded, qreal defaultSplitRatio,
                       int depth = 0)
{
    if (!node || node->isLeaf() || depth >= AutotileDefaults::MaxRuntimeTreeDepth) {
        return;
    }
    const SplitNode* first = node->first.get();
    const SplitNode* second = node->second.get();
    if (first && second && first->isLeaf() && second->isLeaf()) {
        auto it = recorded.constFind(first->windowId);
        if (it != recorded.constEnd() && it->siblingIsLeaf && it->siblingId == second->windowId) {
            node->splitRatio = std::clamp(it->ratio, AutotileDefaults::MinSplitRatio, AutotileDefaults::MaxSplitRatio);
            node->splitHorizontal = it->horizontal;
        } else {
            node->splitRatio = defaultSplitRatio;
        }
    }
    // Recurse so we reach every single-leaf-pair split in the new tree.
    restoreLeafRatios(node->first.get(), recorded, defaultSplitRatio, depth + 1);
    restoreLeafRatios(node->second.get(), recorded, defaultSplitRatio, depth + 1);
}

} // anonymous namespace

/**
 * @brief Rebuild the split tree from a new window order, preserving split ratios where possible
 *
 * Builds a fresh tree by inserting windows in the given order, then restores split
 * ratios by matching window identities rather than tree positions.
 *
 * Ratio preservation semantics:
 *  - Before rebuild, walk the OLD tree and record, for each leaf, the ratio +
 *    direction of the split immediately above it and the id of its sibling.
 *  - After rebuild, walk the NEW tree. For every internal node whose BOTH
 *    children are single-leaf nodes, look up one of the leaf ids in the record
 *    map; if the recorded sibling matches the other child, apply the recorded
 *    ratio and direction.
 *  - All other internal nodes (splits that govern multi-leaf subtrees, or
 *    leaves that appeared without a matching sibling) fall back to
 *    @p defaultSplitRatio — the user never tuned a ratio for a pair that
 *    doesn't exist in the new tree.
 *
 * This means a ratio tuned for the pair (A|B) is only restored if (A,B) are
 * again paired as direct siblings in the new tree. Reordering that breaks the
 * pair cleanly resets that ratio to default instead of silently remapping it
 * onto an unrelated pair.
 *
 * @param tiledWindows Ordered list of tiled window IDs (duplicates and empties are filtered)
 * @param defaultSplitRatio Default split ratio for new internal nodes
 */
bool SplitTree::rebuildFromOrder(const QStringList& tiledWindows, qreal defaultSplitRatio)
{
    if (tiledWindows.isEmpty()) {
        m_root.reset();
        return true;
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
        return true;
    }

    // Cap to prevent degenerate trees exceeding MaxRuntimeTreeDepth.
    // insertAtEndRaw builds a right-leaning chain where N leaves = height N,
    // so N must not exceed MaxRuntimeTreeDepth (recursive traversals bail at depth > MaxRuntimeTreeDepth).
    bool truncated = false;
    if (uniqueWindows.size() > MaxRuntimeTreeDepth) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "rebuildFromOrder: truncating" << uniqueWindows.size() << "windows to" << MaxRuntimeTreeDepth;
        uniqueWindows.resize(MaxRuntimeTreeDepth);
        truncated = true;
    }

    if (uniqueWindows.size() == 1) {
        auto singleTree = std::make_unique<SplitNode>();
        singleTree->windowId = uniqueWindows.first();
        m_root = std::move(singleTree);
        return !truncated;
    }

    // Capture per-leaf split records from the old tree (keyed by windowId).
    QHash<QString, LeafRatioRecord> recorded;
    collectLeafRatios(m_root.get(), recorded);

    // Build a fresh tree from deduplicated input (skip duplicate checks — already deduplicated)
    m_root.reset();
    for (const QString& windowId : uniqueWindows) {
        insertAtEndRaw(windowId, defaultSplitRatio);
    }

    // Apply recorded ratios only where both leaves of a new split are the
    // same pair the user actually tuned.
    restoreLeafRatios(m_root.get(), recorded, defaultSplitRatio);

    return !truncated;
}

// collectInternalNodeParams / applyInternalNodeParams removed — the
// positional-restoration path silently remapped user-tuned ratios onto
// unrelated window pairs after reorder. Ratios are now restored by identity
// via collectLeafRatios / restoreLeafRatios above.

} // namespace PhosphorTiles
