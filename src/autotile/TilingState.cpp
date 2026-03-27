// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TilingState.h"
#include "SplitTree.h"
#include "core/constants.h"
#include "core/logging.h"
#include <QJsonArray>
#include <QtMath>
#include <algorithm>

namespace PlasmaZones {

// Use shared JSON keys from constants.h
using namespace AutotileJsonKeys;
using namespace AutotileDefaults;

TilingState::TilingState(const QString& screenId, QObject* parent)
    : QObject(parent)
    , m_screenId(screenId)
{
}

TilingState::~TilingState() = default;

QString TilingState::screenId() const
{
    return m_screenId;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Order Management
// ═══════════════════════════════════════════════════════════════════════════════

int TilingState::windowCount() const
{
    return m_windowOrder.size();
}

int TilingState::tiledWindowCount() const
{
    int count = 0;
    for (const QString& id : m_windowOrder) {
        if (!m_floatingWindows.contains(id)) {
            ++count;
        }
    }
    return count;
}

QStringList TilingState::windowOrder() const
{
    return m_windowOrder;
}

QStringList TilingState::tiledWindows() const
{
    QStringList tiled;
    tiled.reserve(m_windowOrder.size());
    for (const QString& id : m_windowOrder) {
        if (!m_floatingWindows.contains(id)) {
            tiled.append(id);
        }
    }
    return tiled;
}

bool TilingState::addWindow(const QString& windowId, int position)
{
    if (windowId.isEmpty() || m_windowOrder.contains(windowId)) {
        return false; // Already tracked or invalid
    }

    const bool appendToEnd = (position < 0 || position >= m_windowOrder.size());

    if (appendToEnd) {
        m_windowOrder.append(windowId);
    } else {
        m_windowOrder.insert(position, windowId);
    }

    if (m_splitTree) {
        if (appendToEnd) {
            m_splitTree->insertAtEnd(windowId, m_splitRatio);
        } else {
            m_splitTree->insertAtPosition(windowId, position, m_splitRatio);
        }
    } else if (tiledWindowCount() >= 2) {
        // Lazily create tree when we reach 2 tiled windows
        m_splitTree = std::make_unique<SplitTree>();
        const auto tiled = tiledWindows();
        for (const auto& id : tiled) {
            m_splitTree->insertAtEnd(id, m_splitRatio);
        }
    }

    Q_EMIT windowCountChanged();
    notifyStateChanged();
    return true;
}

bool TilingState::removeWindow(const QString& windowId)
{
    const int index = m_windowOrder.indexOf(windowId);
    if (index < 0) {
        return false;
    }

    // Remove from list first, then from tree, before emitting signals
    m_windowOrder.removeAt(index);

    if (m_splitTree) {
        m_splitTree->remove(windowId);
    }

    // F7 fix: Emit floatingChanged when removing a floating window so listeners
    // (e.g., the daemon's windowFloatingChanged handler) can propagate the state change
    bool wasFloating = m_floatingWindows.remove(windowId);
    if (wasFloating) {
        Q_EMIT floatingChanged(windowId, false);
    }

    if (m_focusedWindow == windowId) {
        m_focusedWindow.clear();
        Q_EMIT focusedWindowChanged();
    }

    Q_EMIT windowCountChanged();
    notifyStateChanged();
    return true;
}

bool TilingState::moveWindow(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_windowOrder.size()) {
        return false;
    }
    if (toIndex < 0 || toIndex >= m_windowOrder.size()) {
        return false;
    }
    if (fromIndex == toIndex) {
        return true; // No-op is still success
    }

    m_windowOrder.move(fromIndex, toIndex);

    rebuildSplitTree();

    Q_EMIT windowOrderChanged();
    notifyStateChanged();
    return true;
}

bool TilingState::swapWindows(int index1, int index2)
{
    if (index1 < 0 || index1 >= m_windowOrder.size()) {
        return false;
    }
    if (index2 < 0 || index2 >= m_windowOrder.size()) {
        return false;
    }
    if (index1 == index2) {
        return true; // No-op is still success
    }

    // Capture IDs BEFORE the swap so we pass the original (pre-swap) values to the tree
    const QString id1 = m_windowOrder.at(index1);
    const QString id2 = m_windowOrder.at(index2);

    m_windowOrder.swapItemsAt(index1, index2);

    if (m_splitTree) {
        m_splitTree->swap(id1, id2);
    }

    Q_EMIT windowOrderChanged();
    notifyStateChanged();
    return true;
}

bool TilingState::swapWindowsById(const QString& windowId1, const QString& windowId2)
{
    const int index1 = m_windowOrder.indexOf(windowId1);
    const int index2 = m_windowOrder.indexOf(windowId2);

    if (index1 < 0 || index2 < 0) {
        return false;
    }
    if (index1 == index2) {
        return true; // Same window, no-op success
    }

    return swapWindows(index1, index2);
}

int TilingState::windowIndex(const QString& windowId) const
{
    return m_windowOrder.indexOf(windowId);
}

bool TilingState::containsWindow(const QString& windowId) const
{
    return m_windowOrder.contains(windowId);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Master Management
// ═══════════════════════════════════════════════════════════════════════════════

int TilingState::masterCount() const
{
    return m_masterCount;
}

void TilingState::setMasterCount(int count)
{
    // Clamp to absolute limits only — do NOT clamp against tiledWindowCount().
    // Algorithms already clamp operationally (e.g., MasterStack: min(masterCount, windowCount)).
    // Clamping here against window count would lose the user's preference when windows
    // are removed and later re-added.
    count = std::clamp(count, MinMasterCount, MaxMasterCount);

    if (m_masterCount != count) {
        m_masterCount = count;
        Q_EMIT masterCountChanged();
        notifyStateChanged();
    }
}

bool TilingState::isMaster(const QString& windowId) const
{
    if (m_floatingWindows.contains(windowId)) {
        return false;
    }

    // Get tiled index
    int tiledIndex = 0;
    for (const QString& id : m_windowOrder) {
        if (m_floatingWindows.contains(id)) {
            continue;
        }
        if (id == windowId) {
            return tiledIndex < m_masterCount;
        }
        ++tiledIndex;
    }
    return false;
}

QStringList TilingState::masterWindows() const
{
    QStringList masters;
    int tiledIndex = 0;
    for (const QString& id : m_windowOrder) {
        if (m_floatingWindows.contains(id)) {
            continue;
        }
        if (tiledIndex < m_masterCount) {
            masters.append(id);
            ++tiledIndex;
        } else {
            break;
        }
    }
    return masters;
}

QStringList TilingState::stackWindows() const
{
    QStringList stack;
    int tiledIndex = 0;
    for (const QString& id : m_windowOrder) {
        if (m_floatingWindows.contains(id)) {
            continue;
        }
        if (tiledIndex >= m_masterCount) {
            stack.append(id);
        }
        ++tiledIndex;
    }
    return stack;
}

bool TilingState::promoteToMaster(const QString& windowId)
{
    const int index = m_windowOrder.indexOf(windowId);
    if (index < 0) {
        return false;
    }

    // Already at position 0
    if (index == 0) {
        return true;
    }

    // Move to front
    m_windowOrder.move(index, 0);

    rebuildSplitTree();

    Q_EMIT windowOrderChanged();
    notifyStateChanged();
    return true;
}

bool TilingState::moveToFront(const QString& windowId)
{
    return promoteToMaster(windowId);
}

bool TilingState::insertAfterFocused(const QString& windowId)
{
    if (windowId.isEmpty() || m_windowOrder.contains(windowId)) {
        return false; // Already tracked or invalid
    }

    // Find position after focused window
    int insertPos = -1; // Default to end
    if (!m_focusedWindow.isEmpty()) {
        const int focusedIndex = m_windowOrder.indexOf(m_focusedWindow);
        if (focusedIndex >= 0) {
            insertPos = focusedIndex + 1;
        }
    }

    return addWindow(windowId, insertPos);
}

bool TilingState::moveToPosition(const QString& windowId, int position)
{
    const int fromIndex = m_windowOrder.indexOf(windowId);
    if (fromIndex < 0) {
        return false;
    }

    return moveWindow(fromIndex, position);
}

int TilingState::windowPosition(const QString& windowId) const
{
    return windowIndex(windowId);
}

int TilingState::tiledWindowIndex(const QString& windowId) const
{
    int tiledIdx = 0;
    for (const auto& id : m_windowOrder) {
        if (m_floatingWindows.contains(id)) {
            continue;
        }
        if (id == windowId) {
            return tiledIdx;
        }
        ++tiledIdx;
    }
    return -1;
}

bool TilingState::moveToTiledPosition(const QString& windowId, int tiledPosition)
{
    // Translate tiledPosition to raw m_windowOrder index
    int rawTarget = -1;
    int tiledIdx = 0;
    for (int i = 0; i < m_windowOrder.size(); ++i) {
        if (m_floatingWindows.contains(m_windowOrder[i])) {
            continue;
        }
        if (tiledIdx == tiledPosition) {
            rawTarget = i;
            break;
        }
        ++tiledIdx;
    }

    // If tiledPosition is past the last tiled window, move to last position
    if (rawTarget < 0) {
        rawTarget = qMax(0, m_windowOrder.size() - 1);
    }

    const int fromIndex = m_windowOrder.indexOf(windowId);
    if (fromIndex < 0) {
        return false;
    }
    return moveWindow(fromIndex, rawTarget);
}

bool TilingState::rotateWindows(bool clockwise)
{
    // Get only tiled (non-floating) windows for rotation
    QStringList tiled = tiledWindows();
    if (tiled.size() < 2) {
        return false; // Nothing to rotate with 0 or 1 tiled window
    }

    // Rotate the tiled windows list
    if (clockwise) {
        // Clockwise: move last element to front
        // [A, B, C] -> [C, A, B]
        QString last = tiled.takeLast();
        tiled.prepend(last);
    } else {
        // Counter-clockwise: move first element to end
        // [A, B, C] -> [B, C, A]
        QString first = tiled.takeFirst();
        tiled.append(first);
    }

    // Rebuild the full window order: keep floating windows at their positions,
    // replace tiled windows with rotated order
    int tiledIndex = 0;
    for (int i = 0; i < m_windowOrder.size() && tiledIndex < tiled.size(); ++i) {
        if (!m_floatingWindows.contains(m_windowOrder[i])) {
            m_windowOrder[i] = tiled[tiledIndex++];
        }
    }

    rebuildSplitTree();

    Q_EMIT windowOrderChanged();
    notifyStateChanged();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Split Ratio
// ═══════════════════════════════════════════════════════════════════════════════

qreal TilingState::splitRatio() const
{
    return m_splitRatio;
}

void TilingState::setSplitRatio(qreal ratio)
{
    // Clamp to valid range using constants
    ratio = std::clamp(ratio, MinSplitRatio, MaxSplitRatio);

    // Use qFuzzyCompare properly (add 1.0 for values near zero)
    if (!qFuzzyCompare(1.0 + m_splitRatio, 1.0 + ratio)) {
        m_splitRatio = ratio;
        Q_EMIT splitRatioChanged();
        notifyStateChanged();
    }
}

void TilingState::increaseSplitRatio(qreal delta)
{
    setSplitRatio(m_splitRatio + delta);
}

void TilingState::decreaseSplitRatio(qreal delta)
{
    setSplitRatio(m_splitRatio - delta);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-Window Floating State
// ═══════════════════════════════════════════════════════════════════════════════

bool TilingState::isFloating(const QString& windowId) const
{
    return m_floatingWindows.contains(windowId);
}

void TilingState::setFloating(const QString& windowId, bool floating)
{
    if (!m_windowOrder.contains(windowId)) {
        return;
    }

    const bool wasFloating = m_floatingWindows.contains(windowId);
    if (wasFloating == floating) {
        return;
    }

    // Compute tiled index BEFORE modifying the floating set, so that
    // tiledWindowIndex() sees the correct floating/tiled classification.
    int tiledIdxBeforeChange = -1;
    if (!floating) {
        // Window is being unfloated — compute where it will land in tiled order
        // after we remove it from the floating set. We temporarily remove it
        // to get the correct index, then restore state.
        // Safety: no signals emitted and no exceptions thrown between remove and re-insert,
        // so the intermediate state is never observed externally.
        m_floatingWindows.remove(windowId);
        tiledIdxBeforeChange = tiledWindowIndex(windowId);
        m_floatingWindows.insert(windowId); // restore — actual removal happens below
    }

    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
    }

    if (m_splitTree) {
        if (floating) {
            m_splitTree->remove(windowId);
        } else {
            // Re-insert at the window's pre-computed tiled position
            if (tiledIdxBeforeChange >= 0) {
                m_splitTree->insertAtPosition(windowId, tiledIdxBeforeChange, m_splitRatio);
            } else {
                m_splitTree->insertAtEnd(windowId, m_splitRatio);
            }
        }
    }

    Q_EMIT floatingChanged(windowId, floating);
    Q_EMIT windowCountChanged(); // Tiled count changed
    notifyStateChanged();
}

bool TilingState::toggleFloating(const QString& windowId)
{
    // Check if window is tracked first
    if (!m_windowOrder.contains(windowId)) {
        return isFloating(windowId); // Return current state (false for untracked)
    }

    const bool newState = !isFloating(windowId);
    setFloating(windowId, newState);
    // Return actual state after operation (in case setFloating had any issues)
    return isFloating(windowId);
}

QStringList TilingState::floatingWindows() const
{
    return QStringList(m_floatingWindows.begin(), m_floatingWindows.end());
}

// ═══════════════════════════════════════════════════════════════════════════════
// Focus Tracking
// ═══════════════════════════════════════════════════════════════════════════════

QString TilingState::focusedWindow() const
{
    return m_focusedWindow;
}

void TilingState::setFocusedWindow(const QString& windowId)
{
    // Allow setting empty (no focus) or a tracked window
    if (!windowId.isEmpty() && !m_windowOrder.contains(windowId)) {
        return;
    }

    if (m_focusedWindow != windowId) {
        m_focusedWindow = windowId;
        Q_EMIT focusedWindowChanged();
    }
}

int TilingState::focusedTiledIndex() const
{
    if (m_focusedWindow.isEmpty() || m_floatingWindows.contains(m_focusedWindow)) {
        return -1;
    }

    int tiledIndex = 0;
    for (const QString& id : m_windowOrder) {
        if (m_floatingWindows.contains(id)) {
            continue;
        }
        if (id == m_focusedWindow) {
            return tiledIndex;
        }
        ++tiledIndex;
    }
    return -1;
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
        json[QLatin1String("splitTree")] = m_splitTree->toJson();
    }

    return json;
}

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
    state->m_masterCount = std::clamp(json[MasterCount].toInt(DefaultMasterCount), MinMasterCount, MaxMasterCount);

    // Split ratio with clamping
    state->m_splitRatio = std::clamp(json[SplitRatio].toDouble(DefaultSplitRatio), MinSplitRatio, MaxSplitRatio);

    if (json.contains(QLatin1String("splitTree"))) {
        state->m_splitTree = SplitTree::fromJson(json[QLatin1String("splitTree")].toObject());
        if (state->m_splitTree) {
            // Validate leaf count matches tiled (non-floating) window count.
            // The split tree only contains tiled windows — floating windows are
            // removed from the tree — so we must compare against tiledWindowCount().
            if (state->m_splitTree->leafCount() != state->tiledWindowCount()) {
                qCWarning(lcAutotile) << "SplitTree leaf count mismatch, discarding tree";
                state->m_splitTree.reset();
            } else {
                // Validate all leaf IDs are present in window order (guards against
                // stale IDs from a replaced window with the same count)
                const QStringList leafIds = state->m_splitTree->leafOrder();
                const QSet<QString> windowSet(state->m_windowOrder.constBegin(), state->m_windowOrder.constEnd());
                bool allMatch = true;
                for (const QString& leafId : leafIds) {
                    if (!windowSet.contains(leafId)) {
                        allMatch = false;
                        break;
                    }
                }
                if (!allMatch) {
                    qCWarning(lcAutotile) << "SplitTree leaf IDs don't match window order, discarding tree";
                    state->m_splitTree.reset();
                }
            }
        }
    }

    return state;
}

void TilingState::clear()
{
    // Track if we need to emit signals
    const bool hadData = !m_windowOrder.isEmpty() || !m_floatingWindows.isEmpty() || !m_focusedWindow.isEmpty()
        || m_masterCount != DefaultMasterCount || !qFuzzyCompare(1.0 + m_splitRatio, 1.0 + DefaultSplitRatio)
        || m_splitTree;

    if (!hadData) {
        return; // Already at defaults, nothing to do
    }

    // Reset all state
    m_windowOrder.clear();
    m_floatingWindows.clear();
    m_focusedWindow.clear();
    m_masterCount = DefaultMasterCount;
    m_splitRatio = DefaultSplitRatio;
    m_splitTree.reset();

    // Emit a single batch of signals
    Q_EMIT windowCountChanged();
    Q_EMIT focusedWindowChanged();
    Q_EMIT masterCountChanged();
    Q_EMIT splitRatioChanged();
    notifyStateChanged();
}

void TilingState::notifyStateChanged()
{
    Q_EMIT stateChanged();
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

    const QStringList tiled = tiledWindows();
    if (tiled.isEmpty()) {
        m_splitTree.reset();
        return;
    }
    if (tiled.size() == 1) {
        // Preserve the tree with a single root leaf so that custom split
        // ratios survive window count transitions (e.g., 3→1→3 windows).
        auto singleTree = std::make_unique<SplitTree>();
        singleTree->insertAtEnd(tiled.first());
        m_splitTree = std::move(singleTree);
        return;
    }

    // Capture existing split ratios from the old tree (indexed by depth-first
    // internal node position) so we can restore them after rebuilding.
    // The new tree has the same binary structure (N-1 internal nodes for N leaves)
    // so positional mapping preserves ratios for the common prefix of nodes.
    const int oldLeafCount = m_splitTree->leafCount();
    QVector<qreal> oldRatios;
    QVector<bool> oldDirections;
    collectInternalNodeParams(m_splitTree->root(), oldRatios, oldDirections);

    auto newTree = std::make_unique<SplitTree>();
    for (const QString& windowId : tiled) {
        newTree->insertAtEnd(windowId);
    }

    // E3: Only restore ratios when the leaf count matches — mismatched sizes
    // produce incorrect positional mapping and corrupt the split layout.
    if (oldLeafCount != tiled.size()) {
        qCDebug(lcAutotile) << "rebuildSplitTree: leaf count changed from" << oldLeafCount << "to" << tiled.size()
                            << "-- skipping ratio restoration";
    } else {
        // Restore saved ratios to the new tree's internal nodes (same traversal order)
        applyInternalNodeParams(newTree->root(), oldRatios, oldDirections, 0);
    }

    m_splitTree = std::move(newTree);
}

void TilingState::collectInternalNodeParams(const SplitNode* node, QVector<qreal>& ratios, QVector<bool>& directions)
{
    if (!node || node->isLeaf()) {
        return;
    }
    ratios.append(node->splitRatio);
    directions.append(node->splitHorizontal);
    collectInternalNodeParams(node->first.get(), ratios, directions);
    collectInternalNodeParams(node->second.get(), ratios, directions);
}

int TilingState::applyInternalNodeParams(SplitNode* node, const QVector<qreal>& ratios, const QVector<bool>& directions,
                                         int index)
{
    if (!node || node->isLeaf()) {
        return index;
    }
    if (index < ratios.size()) {
        node->splitRatio = ratios[index];
        node->splitHorizontal = directions[index];
    }
    ++index;
    index = applyInternalNodeParams(node->first.get(), ratios, directions, index);
    index = applyInternalNodeParams(node->second.get(), ratios, directions, index);
    return index;
}

} // namespace PlasmaZones
