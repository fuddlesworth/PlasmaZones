// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TilingState.h"
#include "core/constants.h"
#include <QJsonArray>
#include <QtMath>
#include <algorithm>

namespace PlasmaZones {

// Use shared JSON keys from constants.h
using namespace AutotileJsonKeys;
using namespace AutotileDefaults;

TilingState::TilingState(const QString& screenName, QObject* parent)
    : QObject(parent)
    , m_screenName(screenName)
{
}

QString TilingState::screenName() const
{
    return m_screenName;
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

    if (position < 0 || position >= m_windowOrder.size()) {
        m_windowOrder.append(windowId);
    } else {
        m_windowOrder.insert(position, windowId);
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
    m_floatingWindows.remove(windowId);

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

    m_windowOrder.swapItemsAt(index1, index2);
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
    // Clamp to valid range: at least MinMasterCount, at most all tiled windows (but not more than MaxMasterCount)
    const int maxAllowed = std::min(MaxMasterCount, std::max(MinMasterCount, tiledWindowCount()));
    count = std::clamp(count, MinMasterCount, maxAllowed);

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

    if (floating) {
        m_floatingWindows.insert(windowId);
    } else {
        m_floatingWindows.remove(windowId);
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
    json[ScreenName] = m_screenName;
    json[WindowOrder] = QJsonArray::fromStringList(m_windowOrder);
    json[FloatingWindows] = QJsonArray::fromStringList(floatingWindows());
    json[FocusedWindow] = m_focusedWindow;
    json[MasterCount] = m_masterCount;
    json[SplitRatio] = m_splitRatio;
    return json;
}

TilingState* TilingState::fromJson(const QJsonObject& json, QObject* parent)
{
    const QString screenName = json[ScreenName].toString();
    if (screenName.isEmpty()) {
        return nullptr;
    }

    auto* state = new TilingState(screenName, parent);

    // Window order
    const QJsonArray orderArray = json[WindowOrder].toArray();
    for (const QJsonValue& val : orderArray) {
        const QString id = val.toString();
        if (!id.isEmpty()) {
            state->m_windowOrder.append(id);
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

    // Master count with clamping
    const int tiledCount = state->tiledWindowCount();
    const int maxMaster = std::min(MaxMasterCount, std::max(MinMasterCount, tiledCount));
    state->m_masterCount = std::clamp(json[MasterCount].toInt(DefaultMasterCount), MinMasterCount, maxMaster);

    // Split ratio with clamping
    state->m_splitRatio = std::clamp(json[SplitRatio].toDouble(DefaultSplitRatio), MinSplitRatio, MaxSplitRatio);

    return state;
}

void TilingState::clear()
{
    // Track if we need to emit signals
    const bool hadData = !m_windowOrder.isEmpty() || !m_floatingWindows.isEmpty() || !m_focusedWindow.isEmpty()
        || m_masterCount != DefaultMasterCount || !qFuzzyCompare(1.0 + m_splitRatio, 1.0 + DefaultSplitRatio);

    if (!hadData) {
        return; // Already at defaults, nothing to do
    }

    // Reset all state
    m_windowOrder.clear();
    m_floatingWindows.clear();
    m_focusedWindow.clear();
    m_masterCount = DefaultMasterCount;
    m_splitRatio = DefaultSplitRatio;

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

} // namespace PlasmaZones
