// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/TilingState.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/SplitTree.h>
#include "tileslogging.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>

namespace PhosphorTiles {

// Bounds for the script-state sanitizer and the master-count / split-ratio clamps.
using namespace AutotileDefaults;

namespace {
// Recursively walk a script-state value, rejecting non-finite numbers and
// over-deep nesting, and counting object keys. Returns false if the value must
// be dropped (too deep). Mutates @p value in place to strip bad leaves.
bool sanitizeScriptValue(QJsonValue& value, int depth, int& keyCount)
{
    if (depth > AutotileDefaults::ScriptStateMaxDepth) {
        return false;
    }
    if (value.isObject()) {
        QJsonObject obj = value.toObject();
        QJsonObject cleaned;
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            if (++keyCount > AutotileDefaults::ScriptStateMaxKeys) {
                break;
            }
            QJsonValue child = it.value();
            if (sanitizeScriptValue(child, depth + 1, keyCount)) {
                cleaned.insert(it.key(), child);
            }
        }
        value = cleaned;
        return true;
    }
    if (value.isArray()) {
        QJsonArray arr = value.toArray();
        QJsonArray cleaned;
        for (QJsonValue child : arr) {
            // Count array elements against the same budget as object keys so a
            // pathologically large flat array bails here rather than being fully
            // materialized and only rejected later by the post-hoc byte cap.
            if (++keyCount > AutotileDefaults::ScriptStateMaxKeys) {
                break;
            }
            if (sanitizeScriptValue(child, depth + 1, keyCount)) {
                cleaned.append(child);
            }
        }
        value = cleaned;
        return true;
    }
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (!std::isfinite(d)) {
            return false; // drop NaN / ±Inf leaves
        }
    }
    return true;
}
} // namespace

QJsonObject TilingState::sanitizeScriptState(const QJsonObject& state)
{
    if (state.isEmpty()) {
        return {};
    }
    QJsonValue root(state);
    int keyCount = 0;
    if (!sanitizeScriptValue(root, 0, keyCount)) {
        return {};
    }
    QJsonObject cleaned = root.toObject();
    // The key/element budget breaks mid-container on overflow, silently dropping
    // the tail. Warn once (here, not in the recursion) so a script that overruns
    // the cap is diagnosable rather than mysteriously losing half its state —
    // matching the byte-cap path's warning below.
    if (keyCount > AutotileDefaults::ScriptStateMaxKeys) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "scriptState exceeds" << AutotileDefaults::ScriptStateMaxKeys << "entries — bag truncated";
    }
    // Byte cap is checked on the post-strip object so a bag that only exceeds
    // it via dropped NaN/garbage can still squeak under.
    const qsizetype bytes = QJsonDocument(cleaned).toJson(QJsonDocument::Compact).size();
    if (bytes > AutotileDefaults::ScriptStateMaxBytes) {
        qCWarning(PhosphorTiles::lcTilesLib) << "scriptState exceeds" << AutotileDefaults::ScriptStateMaxBytes
                                             << "bytes (" << bytes << ") — dropping bag";
        return {};
    }
    return cleaned;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Clamping Helpers (DRY: shared by the setters and the script-state sanitizer)
// ═══════════════════════════════════════════════════════════════════════════════

int TilingState::clampMasterCount(int value)
{
    return std::clamp(value, MinMasterCount, MaxMasterCount);
}

qreal TilingState::clampSplitRatio(qreal value)
{
    return std::clamp(value, MinSplitRatio, MaxSplitRatio);
}

void TilingState::clear()
{
    // Track each field's old value before clearing so we only emit signals for actual changes
    const bool hadWindows = !m_windowOrder.isEmpty() || !m_floatingWindows.isEmpty();
    const bool hadFocused = !m_focusedWindow.isEmpty();
    const int oldMasterCount = m_masterCount;
    const qreal oldSplitRatio = m_splitRatio;

    if (!hadWindows && !hadFocused && m_calculatedZones.isEmpty() && oldMasterCount == DefaultMasterCount
        && qFuzzyCompare(1.0 + oldSplitRatio, 1.0 + DefaultSplitRatio) && !m_splitTree && m_scriptState.isEmpty()) {
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
    // The opaque script bag is part of the state's identity — a cleared state
    // must not retain a scripted algorithm's persistent memory. Reset without a
    // signal, matching setScriptState's deliberately NOTIFY-free contract.
    m_scriptState = QJsonObject{};

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

QJsonObject TilingState::scriptState() const
{
    return m_scriptState;
}

void TilingState::setScriptState(const QJsonObject& state)
{
    // No NOTIFY / notifyStateChanged: persistence only. Emitting a change here
    // could trigger a retile, and the scripted write-back path runs from inside
    // a resize retile — re-entering would risk a resize→retile→resize loop.
    m_scriptState = state;
}

void TilingState::clearSplitTree()
{
    m_splitTree.reset();
}

std::unique_ptr<SplitTree> TilingState::takeSplitTree()
{
    return std::move(m_splitTree);
}

void TilingState::rebuildSplitTree()
{
    if (!m_splitTree) {
        return; // No tree to rebuild
    }

    const bool fullyRebuilt = m_splitTree->rebuildFromOrder(tiledWindows(), m_splitRatio);
    if (!fullyRebuilt) {
        // rebuildFromOrder truncated to MaxRuntimeTreeDepth. Clamp m_windowOrder
        // to match so the TilingState invariant (tiledWindows() ⊆ tree leaves)
        // is preserved — otherwise the next syncTreeInsert would insert against
        // a mismatched position index.
        const QStringList keptLeaves = m_splitTree->leafOrder();
        const QSet<QString> keepSet(keptLeaves.begin(), keptLeaves.end());
        QStringList clamped;
        clamped.reserve(m_windowOrder.size());
        for (const QString& id : m_windowOrder) {
            if (m_floatingWindows.contains(id) || keepSet.contains(id)) {
                clamped.append(id);
            }
        }
        if (clamped.size() != m_windowOrder.size()) {
            m_windowOrder = clamped;
            Q_EMIT windowCountChanged();
            Q_EMIT windowOrderChanged();
        }
    }
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
// Uses the library default split ratio unless m_splitRatio is already close to
// it (meaning the user or prepareTilingState already set it), in which case we
// use m_splitRatio to honor any fine-tuning the user applied.
void TilingState::syncTreeLazyCreate()
{
    if (m_splitTree || tiledWindowCount() < 2) {
        return;
    }
    // Use m_splitRatio if it's within the hysteresis band of the default,
    // meaning prepareTilingState has likely already run. Otherwise fall back
    // to the default to avoid building a tree with a stale MasterStack ratio.
    const qreal ratio =
        (std::abs(m_splitRatio - DefaultSplitRatio) <= SplitRatioHysteresis) ? m_splitRatio : DefaultSplitRatio;
    m_splitTree = std::make_unique<SplitTree>();
    const auto tiled = tiledWindows();
    for (const auto& id : tiled) {
        m_splitTree->insertAtEnd(id, ratio);
    }
}

} // namespace PhosphorTiles
