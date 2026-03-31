// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TilingState.h"
#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"
#include <QJsonArray>

namespace PlasmaZones {

// Use shared JSON keys from constants.h
using namespace AutotileJsonKeys;
using namespace AutotileDefaults;

// ═══════════════════════════════════════════════════════════════════════════════
// Clamping Helpers (DRY: shared by setters and fromJson)
// ═══════════════════════════════════════════════════════════════════════════════

int TilingState::clampMasterCount(int value)
{
    return std::clamp(value, MinMasterCount, MaxMasterCount);
}

qreal TilingState::clampSplitRatio(qreal value)
{
    return std::clamp(value, MinSplitRatio, MaxSplitRatio);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Serialization
// ═══════════════════════════════════════════════════════════════════════════════

QJsonObject TilingState::toJson() const
{
    QJsonObject json;
    json[ScreenName] = m_screenId;
    json[WindowOrder] = QJsonArray::fromStringList(m_windowOrder);
    json[FloatingWindows] = QJsonArray::fromStringList(floatingWindows());
    json[FocusedWindow] = m_focusedWindow;
    json[MasterCount] = m_masterCount;
    json[SplitRatio] = m_splitRatio;

    if (m_splitTree && !m_splitTree->isEmpty()) {
        json[AutotileJsonKeys::SplitTreeKey] = m_splitTree->toJson();
    }

    return json;
}

// Ownership: caller takes ownership (Qt parent set if provided)
TilingState* TilingState::fromJson(const QJsonObject& json, QObject* parent)
{
    const QString screenId = json[ScreenName].toString();
    if (screenId.isEmpty()) {
        return nullptr;
    }

    auto* state = new TilingState(screenId, parent);

    // Window order (deduplicate to guard against corrupt JSON)
    const QJsonArray orderArray = json[WindowOrder].toArray();
    QSet<QString> seenIds;
    seenIds.reserve(orderArray.size());
    for (const QJsonValue& val : orderArray) {
        const QString id = val.toString();
        if (!id.isEmpty() && !seenIds.contains(id)) {
            state->m_windowOrder.append(id);
            seenIds.insert(id);
        }
    }

    // Floating windows (validate they exist in window order)
    const QJsonArray floatingArray = json[FloatingWindows].toArray();
    for (const QJsonValue& val : floatingArray) {
        const QString id = val.toString();
        if (!id.isEmpty() && state->m_windowOrder.contains(id)) {
            state->m_floatingWindows.insert(id);
        }
    }

    // Focused window (validate it exists in window order)
    const QString focusedId = json[FocusedWindow].toString();
    if (state->m_windowOrder.contains(focusedId)) {
        state->m_focusedWindow = focusedId;
    }
    // else: leave empty (invalid focused window in JSON)

    // Master count — clamp to absolute limits only (not against current window count).
    // Algorithms clamp operationally when they calculate zones.
    state->m_masterCount = clampMasterCount(json[MasterCount].toInt(DefaultMasterCount));

    // Split ratio with clamping and sanity check for obviously corrupted values
    // (e.g., values near 1.0 that indicate state corruption rather than user intent).
    // Only apply the reset to values WITHIN the valid range but suspiciously close to max,
    // not to completely out-of-range values (those get clamped normally per the test).
    qreal loadedRatio = json[SplitRatio].toDouble(DefaultSplitRatio);
    const bool withinRange = (loadedRatio >= MinSplitRatio && loadedRatio <= MaxSplitRatio);
    if (withinRange && loadedRatio > MaxSplitRatio - 0.05) {
        qCDebug(lcAutotile) << "SplitRatio suspiciously high, resetting to default:" << loadedRatio;
        loadedRatio = DefaultSplitRatio;
    }
    state->m_splitRatio = clampSplitRatio(loadedRatio);

    if (json.contains(AutotileJsonKeys::SplitTreeKey)) {
        state->m_splitTree = SplitTree::fromJson(json[AutotileJsonKeys::SplitTreeKey].toObject());
        if (state->m_splitTree) {
            // Single-pass leaf validation — checks count, membership, and duplicates together.
            // The split tree only contains tiled windows — floating windows are
            // removed from the tree — so we must compare against tiledWindowCount().
            const QStringList leafIds = state->m_splitTree->leafOrder();
            if (leafIds.size() != state->tiledWindowCount()) {
                qCWarning(lcAutotile) << "SplitTree leaf count mismatch, discarding tree";
                state->m_splitTree.reset();
            } else {
                const QStringList tiledWins = state->tiledWindows();
                const QSet<QString> windowSet(tiledWins.begin(), tiledWins.end());
                QSet<QString> seen;
                seen.reserve(leafIds.size());
                bool valid = true;
                for (const QString& id : leafIds) {
                    if (!windowSet.contains(id) || seen.contains(id)) {
                        valid = false;
                        break;
                    }
                    seen.insert(id);
                }
                if (!valid) {
                    qCWarning(lcAutotile) << "SplitTree leaf IDs invalid (missing or duplicate), discarding tree";
                    state->m_splitTree.reset();
                }
            }
        }
    }

    return state;
}

void TilingState::clear()
{
    // Track each field's old value before clearing so we only emit signals for actual changes
    const bool hadWindows = !m_windowOrder.isEmpty() || !m_floatingWindows.isEmpty();
    const bool hadFocused = !m_focusedWindow.isEmpty();
    const int oldMasterCount = m_masterCount;
    const qreal oldSplitRatio = m_splitRatio;

    if (!hadWindows && !hadFocused && m_calculatedZones.isEmpty() && oldMasterCount == DefaultMasterCount
        && qFuzzyCompare(1.0 + oldSplitRatio, 1.0 + DefaultSplitRatio) && !m_splitTree) {
        return; // Already at defaults, nothing to do
    }

    // Reset all state
    m_windowOrder.clear();
    m_floatingWindows.clear();
    m_focusedWindow.clear();
    m_calculatedZones.clear();
    m_masterCount = DefaultMasterCount;
    m_splitRatio = DefaultSplitRatio;
    m_splitTree.reset();

    // Only emit signals for fields that actually changed
    if (hadWindows) {
        Q_EMIT windowCountChanged();
        Q_EMIT windowOrderChanged();
    }
    if (hadFocused) {
        Q_EMIT focusedWindowChanged();
    }
    if (oldMasterCount != DefaultMasterCount) {
        Q_EMIT masterCountChanged();
    }
    if (!qFuzzyCompare(1.0 + oldSplitRatio, 1.0 + DefaultSplitRatio)) {
        Q_EMIT splitRatioChanged();
    }
    notifyStateChanged();
}

// ═══════════════════════════════════════════════════════════════════════════════
// Calculated Zone Storage
// ═══════════════════════════════════════════════════════════════════════════════

void TilingState::setCalculatedZones(const QVector<QRect>& zones)
{
    m_calculatedZones = zones;
}

QVector<QRect> TilingState::calculatedZones() const
{
    return m_calculatedZones;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split Tree
// ═══════════════════════════════════════════════════════════════════════════════

SplitTree* TilingState::splitTree() const
{
    return m_splitTree.get();
}

void TilingState::setSplitTree(std::unique_ptr<SplitTree> tree)
{
    m_splitTree = std::move(tree);
}

void TilingState::clearSplitTree()
{
    m_splitTree.reset();
}

void TilingState::rebuildSplitTree()
{
    if (!m_splitTree) {
        return; // No tree to rebuild
    }

    m_splitTree->rebuildFromOrder(tiledWindows(), m_splitRatio);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tree Synchronization Helpers
// ═══════════════════════════════════════════════════════════════════════════════

void TilingState::syncTreeInsert(const QString& windowId, int position)
{
    if (!m_splitTree) {
        return;
    }
    if (position < 0) {
        m_splitTree->insertAtEnd(windowId, m_splitRatio);
    } else {
        // Translate raw m_windowOrder index to tiled-only index: the split tree
        // contains only tiled (non-floating) windows, so we must skip floating
        // windows that precede `position` in m_windowOrder.
        // NOTE: The newly inserted window is already at m_windowOrder[position],
        // so we must exclude it from the count to avoid an off-by-one error.
        int tiledPos = 0;
        const int limit = qMin(position, m_windowOrder.size());
        for (int i = 0; i < limit; ++i) {
            const QString& id = m_windowOrder.at(i);
            if (id != windowId && !m_floatingWindows.contains(id)) {
                ++tiledPos;
            }
        }
        m_splitTree->insertAtPosition(windowId, tiledPos, m_splitRatio);
    }
}

void TilingState::syncTreeRemove(const QString& windowId)
{
    if (!m_splitTree) {
        return;
    }
    m_splitTree->remove(windowId);
}

void TilingState::syncTreeSwap(const QString& idA, const QString& idB)
{
    if (!m_splitTree) {
        return;
    }
    m_splitTree->swap(idA, idB);
}

// Lazy-create the split tree when a memory algorithm needs one but none exists
// (e.g., first retile after switching to DwindleMemory).
// Uses the dwindle default (0.5) unless m_splitRatio is already close to it
// (meaning the user or prepareTilingState already set it), in which case we
// use m_splitRatio to honor any fine-tuning the user applied.
void TilingState::syncTreeLazyCreate()
{
    if (m_splitTree || tiledWindowCount() < 2) {
        return;
    }
    // Use m_splitRatio if it's close to the dwindle default (0.5), meaning
    // prepareTilingState has likely already run. Otherwise fall back to 0.5
    // to avoid building a tree with a stale MasterStack ratio (0.6).
    constexpr qreal DwindleDefault = 0.5;
    const qreal ratio = (std::abs(m_splitRatio - DwindleDefault) <= 0.05) ? m_splitRatio : DwindleDefault;
    m_splitTree = std::make_unique<SplitTree>();
    const auto tiled = tiledWindows();
    for (const auto& id : tiled) {
        m_splitTree->insertAtEnd(id, ratio);
    }
}

} // namespace PlasmaZones
