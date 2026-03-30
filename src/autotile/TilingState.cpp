// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TilingState.h"
#include "SplitTree.h"
#include "core/constants.h"
#include <QtMath>
#include <algorithm>

namespace PlasmaZones {

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

// ── Window Order Management ──────────────────────────────────────────────────

int TilingState::windowCount() const
{
    return m_windowOrder.size();
}

int TilingState::tiledWindowCount() const
{
    int count = 0;
    forEachTiledWindow([&count](const QString& /*id*/, int /*idx*/) {
        ++count;
        return true;
    });
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
    forEachTiledWindow([&tiled](const QString& id, int /*idx*/) {
        tiled.append(id);
        return true;
    });
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
        syncTreeInsert(windowId, appendToEnd ? -1 : position);
    } else {
        syncTreeLazyCreate();
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

    m_windowOrder.removeAt(index);
    syncTreeRemove(windowId);

    // Emit floatingChanged so listeners can clean up floating-specific state
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
    if (fromIndex < 0 || fromIndex >= m_windowOrder.size() || toIndex < 0 || toIndex >= m_windowOrder.size()) {
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
    if (index1 < 0 || index1 >= m_windowOrder.size() || index2 < 0 || index2 >= m_windowOrder.size()) {
        return false;
    }
    if (index1 == index2) {
        return true; // No-op is still success
    }

    const QString id1 = m_windowOrder.at(index1);
    const QString id2 = m_windowOrder.at(index2);

    m_windowOrder.swapItemsAt(index1, index2);

    syncTreeSwap(id1, id2);

    Q_EMIT windowOrderChanged();
    notifyStateChanged();
    return true;
}

bool TilingState::swapWindowsById(const QString& windowId1, const QString& windowId2)
{
    const int index1 = m_windowOrder.indexOf(windowId1);
    const int index2 = m_windowOrder.indexOf(windowId2);

    if (index1 < 0 || index2 < 0) {
        return false; // One or both windows not tracked
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

// ── Master Management ────────────────────────────────────────────────────────

int TilingState::masterCount() const
{
    return m_masterCount;
}

void TilingState::setMasterCount(int count)
{
    // Clamp to absolute limits only — algorithms clamp operationally against window count
    count = clampMasterCount(count);

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

    bool result = false;
    forEachTiledWindow([&](const QString& id, int tiledIndex) {
        if (id == windowId) {
            result = tiledIndex < m_masterCount;
            return false; // stop
        }
        return true;
    });
    return result;
}

QStringList TilingState::masterWindows() const
{
    QStringList masters;
    forEachTiledWindow([&](const QString& id, int tiledIndex) {
        if (tiledIndex < m_masterCount) {
            masters.append(id);
            return true;
        }
        return false; // past master area, stop
    });
    return masters;
}

QStringList TilingState::stackWindows() const
{
    QStringList stack;
    forEachTiledWindow([&](const QString& id, int tiledIndex) {
        if (tiledIndex >= m_masterCount) {
            stack.append(id);
        }
        return true;
    });
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

    int insertPos = -1;
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

    const int clampedPos = std::clamp(position, 0, static_cast<int>(m_windowOrder.size()) - 1);
    return moveWindow(fromIndex, clampedPos);
}

int TilingState::windowPosition(const QString& windowId) const
{
    return windowIndex(windowId);
}

int TilingState::tiledWindowIndex(const QString& windowId) const
{
    int result = -1;
    forEachTiledWindow([&](const QString& id, int tiledIndex) {
        if (id == windowId) {
            result = tiledIndex;
            return false; // stop
        }
        return true;
    });
    return result;
}

bool TilingState::moveToTiledPosition(const QString& windowId, int tiledPosition)
{
    // Translate tiledPosition to raw m_windowOrder index using forEachTiledWindow
    int rawTarget = -1;
    forEachTiledWindow([&](const QString& id, int tiledIdx) {
        if (tiledIdx == tiledPosition) {
            rawTarget = m_windowOrder.indexOf(id);
            return false; // stop
        }
        return true;
    });

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
    QStringList tiled = tiledWindows();
    if (tiled.size() < 2) {
        return false;
    }

    if (clockwise) {
        tiled.prepend(tiled.takeLast()); // [A,B,C] -> [C,A,B]
    } else {
        tiled.append(tiled.takeFirst()); // [A,B,C] -> [B,C,A]
    }

    // Replace tiled slots in m_windowOrder, preserving floating positions
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

// ── Split Ratio ──────────────────────────────────────────────────────────────

qreal TilingState::splitRatio() const
{
    return m_splitRatio;
}

void TilingState::setSplitRatio(qreal ratio)
{
    // Clamp to valid range using constants
    ratio = clampSplitRatio(ratio);

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

// ── Per-Window Floating State ─────────────────────────────────────────────────

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

    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
    }

    if (floating) {
        syncTreeRemove(windowId);
    } else {
        // Rebuild the entire tree rather than incremental insert to avoid
        // tree/list ordering divergence after multiple float/unfloat cycles.
        rebuildSplitTree();
    }

    Q_EMIT floatingChanged(windowId, floating);
    Q_EMIT windowCountChanged(); // Tiled count changed
    notifyStateChanged();
}

// Returns the new floating state after toggle, or false if the window is untracked.
// Note: false is ambiguous (could mean "not floating" or "untracked"). Callers
// should check windowOrder membership first if the distinction matters.
bool TilingState::toggleFloating(const QString& windowId)
{
    if (!m_windowOrder.contains(windowId)) {
        return false; // Untracked window
    }
    setFloating(windowId, !isFloating(windowId));
    return isFloating(windowId);
}

QStringList TilingState::floatingWindows() const
{
    QStringList list(m_floatingWindows.begin(), m_floatingWindows.end());
    // Sort for deterministic serialization — QSet iteration order is unstable
    // across Qt versions and hash seed randomization.
    list.sort();
    return list;
}

// ── Focus Tracking ───────────────────────────────────────────────────────────

QString TilingState::focusedWindow() const
{
    return m_focusedWindow;
}

void TilingState::setFocusedWindow(const QString& windowId)
{
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

    return tiledWindowIndex(m_focusedWindow);
}

void TilingState::notifyStateChanged()
{
    Q_EMIT stateChanged();
}

// ── forEachTiledWindow — DRY helper for tiled-window iteration ───────────────

void TilingState::forEachTiledWindow(const std::function<bool(const QString& windowId, int tiledIndex)>& func) const
{
    int tiledIndex = 0;
    for (const QString& id : m_windowOrder) {
        if (m_floatingWindows.contains(id)) {
            continue;
        }
        if (!func(id, tiledIndex)) {
            return;
        }
        ++tiledIndex;
    }
}

} // namespace PlasmaZones
