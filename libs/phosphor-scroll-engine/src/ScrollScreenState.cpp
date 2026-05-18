// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollScreenState.h>

#include <QJsonArray>

#include <utility>

namespace PhosphorScrollEngine {

ScrollScreenState::ScrollScreenState(QString screenId)
    : m_screenId(std::move(screenId))
{
}

const Column* ScrollScreenState::activeColumn() const
{
    if (m_activeColumnIndex < 0 || m_activeColumnIndex >= m_columns.size()) {
        return nullptr;
    }
    return &m_columns.at(m_activeColumnIndex);
}

QString ScrollScreenState::focusedWindowId() const
{
    const Column* column = activeColumn();
    if (!column) {
        return QString();
    }
    const Tile* tile = column->activeTile();
    return tile ? tile->windowId : QString();
}

void ScrollScreenState::addColumnForWindow(const QString& windowId)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return;
    }
    Column column;
    column.appendTile(Tile{windowId, WindowHeight::automatic()});
    const int insertAt = (m_activeColumnIndex < 0) ? 0 : m_activeColumnIndex + 1;
    m_columns.insert(insertAt, std::move(column));
    m_activeColumnIndex = insertAt;
}

void ScrollScreenState::addWindowToActiveColumn(const QString& windowId)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return;
    }
    if (m_activeColumnIndex < 0) {
        addColumnForWindow(windowId);
        return;
    }
    Column& column = m_columns[m_activeColumnIndex];
    column.appendTile(Tile{windowId, WindowHeight::automatic()});
    column.setActiveTileIndex(column.tileCount() - 1);
}

bool ScrollScreenState::removeWindow(const QString& windowId)
{
    if (m_floatingWindows.remove(windowId)) {
        return true;
    }
    const int columnIndex = locateWindow(windowId).first;
    if (columnIndex < 0) {
        return false;
    }
    const QString focused = focusedWindowId();
    m_columns[columnIndex].removeWindow(windowId);
    if (m_columns.at(columnIndex).isEmpty()) {
        m_columns.removeAt(columnIndex);
    }
    refocus(focused);
    return true;
}

bool ScrollScreenState::setWindowMinimized(const QString& windowId, bool minimized)
{
    const int columnIndex = locateWindow(windowId).first;
    if (columnIndex < 0) {
        return false;
    }
    if (!m_columns[columnIndex].setWindowMinimized(windowId, minimized)) {
        return false; // flag already in the requested state
    }
    // The focused window must stay visible: the viewport offset and keyboard
    // navigation anchor on the focused column/tile. Hand focus to the nearest
    // still-visible window when the one being minimized was focused. (A
    // subsequent windowFocused event from the compositor may refine this.)
    if (minimized && focusedWindowId() == windowId) {
        const QString visible = firstVisibleWindowId();
        if (!visible.isEmpty()) {
            focusWindow(visible);
        }
    }
    return true;
}

bool ScrollScreenState::isWindowMinimized(const QString& windowId) const
{
    const int columnIndex = locateWindow(windowId).first;
    return columnIndex >= 0 && m_columns.at(columnIndex).isWindowMinimized(windowId);
}

bool ScrollScreenState::consumeIntoColumn()
{
    if (m_activeColumnIndex < 0) {
        return false;
    }
    const int nextIndex = m_activeColumnIndex + 1;
    if (nextIndex >= m_columns.size()) {
        return false;
    }
    const Tile* source = m_columns.at(nextIndex).activeTile();
    if (!source) {
        return false;
    }
    const Tile moved = *source;
    m_columns[nextIndex].removeWindow(moved.windowId);

    Column& active = m_columns[m_activeColumnIndex];
    active.appendTile(moved);
    active.setActiveTileIndex(active.tileCount() - 1);

    if (m_columns.at(nextIndex).isEmpty()) {
        m_columns.removeAt(nextIndex);
    }
    return true;
}

bool ScrollScreenState::expelFromColumn()
{
    if (m_activeColumnIndex < 0 || m_columns.at(m_activeColumnIndex).tileCount() <= 1) {
        return false;
    }
    const Tile* source = m_columns.at(m_activeColumnIndex).activeTile();
    if (!source) {
        return false;
    }
    const Tile moved = *source;
    m_columns[m_activeColumnIndex].removeWindow(moved.windowId);

    Column column;
    column.appendTile(moved);
    const int insertAt = m_activeColumnIndex + 1;
    m_columns.insert(insertAt, std::move(column));
    m_activeColumnIndex = insertAt;
    return true;
}

bool ScrollScreenState::focusColumn(int delta)
{
    if (m_columns.isEmpty()) {
        return false;
    }
    const int target = qBound(0, m_activeColumnIndex + delta, columnCount() - 1);
    if (target == m_activeColumnIndex) {
        return false;
    }
    m_activeColumnIndex = target;
    return true;
}

bool ScrollScreenState::focusTile(int delta)
{
    if (m_activeColumnIndex < 0) {
        return false;
    }
    Column& column = m_columns[m_activeColumnIndex];
    if (column.isEmpty()) {
        return false;
    }
    const int target = qBound(0, column.activeTileIndex() + delta, column.tileCount() - 1);
    if (target == column.activeTileIndex()) {
        return false;
    }
    column.setActiveTileIndex(target);
    return true;
}

bool ScrollScreenState::focusWindow(const QString& windowId)
{
    const auto [columnIndex, tileIndex] = locateWindow(windowId);
    if (columnIndex < 0) {
        return false;
    }
    m_activeColumnIndex = columnIndex;
    m_columns[columnIndex].setActiveTileIndex(tileIndex);
    return true;
}

bool ScrollScreenState::moveColumn(int delta)
{
    if (m_activeColumnIndex < 0) {
        return false;
    }
    const int target = qBound(0, m_activeColumnIndex + delta, columnCount() - 1);
    if (target == m_activeColumnIndex) {
        return false;
    }
    const Column column = m_columns.takeAt(m_activeColumnIndex);
    m_columns.insert(target, column);
    m_activeColumnIndex = target;
    return true;
}

bool ScrollScreenState::moveTile(int delta)
{
    if (m_activeColumnIndex < 0) {
        return false;
    }
    Column& column = m_columns[m_activeColumnIndex];
    if (column.isEmpty()) {
        return false;
    }
    const int target = qBound(0, column.activeTileIndex() + delta, column.tileCount() - 1);
    return column.moveTile(column.activeTileIndex(), target);
}

bool ScrollScreenState::moveColumnNextTo(const QString& draggedWindowId, const QString& anchorWindowId, bool placeAfter)
{
    const int from = locateWindow(draggedWindowId).first;
    const int anchorColumn = locateWindow(anchorWindowId).first;
    if (from < 0 || anchorColumn < 0 || from == anchorColumn) {
        return false;
    }
    const Column column = m_columns.takeAt(from);
    // Removing the dragged column shifts every later index left by one, so the
    // anchor's index moves too when it sat to the right of the removed column.
    const int anchorAfterRemoval = (anchorColumn > from) ? anchorColumn - 1 : anchorColumn;
    const int target =
        qBound(0, placeAfter ? anchorAfterRemoval + 1 : anchorAfterRemoval, static_cast<int>(m_columns.size()));
    m_columns.insert(target, column);
    // The user just dragged this window — focus it (and its column).
    focusWindow(draggedWindowId);
    return true;
}

void ScrollScreenState::setActiveColumnWidth(const ColumnWidth& width, int presetIndex)
{
    if (m_activeColumnIndex >= 0) {
        m_columns[m_activeColumnIndex].setWidth(width);
        m_columns[m_activeColumnIndex].setPresetWidthIndex(presetIndex);
    }
}

void ScrollScreenState::setActiveTileHeight(const WindowHeight& height)
{
    if (m_activeColumnIndex >= 0) {
        m_columns[m_activeColumnIndex].setActiveTileHeight(height);
    }
}

void ScrollScreenState::markFloating(const QString& windowId)
{
    if (windowId.isEmpty()) {
        return;
    }
    removeWindow(windowId);
    m_floatingWindows.insert(windowId);
}

void ScrollScreenState::clearFloating(const QString& windowId)
{
    m_floatingWindows.remove(windowId);
}

QString ScrollScreenState::screenId() const
{
    return m_screenId;
}

int ScrollScreenState::windowCount() const
{
    return tiledWindowCount() + static_cast<int>(m_floatingWindows.size());
}

QStringList ScrollScreenState::managedWindows() const
{
    QStringList ids;
    for (const Column& column : m_columns) {
        ids += column.windowIds();
    }
    for (const QString& id : m_floatingWindows) {
        ids.append(id);
    }
    return ids;
}

bool ScrollScreenState::containsWindow(const QString& windowId) const
{
    return m_floatingWindows.contains(windowId) || locateWindow(windowId).first >= 0;
}

bool ScrollScreenState::isFloating(const QString& windowId) const
{
    return m_floatingWindows.contains(windowId);
}

QStringList ScrollScreenState::floatingWindows() const
{
    // Sorted so the result is deterministic — QSet iteration order is not.
    QStringList ids(m_floatingWindows.cbegin(), m_floatingWindows.cend());
    ids.sort();
    return ids;
}

QString ScrollScreenState::placementIdForWindow(const QString& windowId) const
{
    const auto [columnIndex, tileIndex] = locateWindow(windowId);
    if (columnIndex < 0) {
        return QString();
    }
    return QString::number(columnIndex) + QLatin1Char(':') + QString::number(tileIndex);
}

int ScrollScreenState::tiledWindowCount() const
{
    int count = 0;
    for (const Column& column : m_columns) {
        count += column.tileCount();
    }
    return count;
}

QJsonObject ScrollScreenState::toJson() const
{
    QJsonArray columns;
    for (const Column& column : m_columns) {
        columns.append(column.toJson());
    }
    // Serialised sorted so toJson() output is byte-stable across runs
    // (QSet iteration order is unspecified).
    QStringList floatingIds(m_floatingWindows.cbegin(), m_floatingWindows.cend());
    floatingIds.sort();
    QJsonArray floating;
    for (const QString& id : floatingIds) {
        floating.append(id);
    }

    QJsonObject obj;
    obj.insert(QLatin1String("screenId"), m_screenId);
    obj.insert(QLatin1String("columns"), columns);
    obj.insert(QLatin1String("activeColumnIndex"), m_activeColumnIndex);
    obj.insert(QLatin1String("scrollX"), m_scrollX);
    obj.insert(QLatin1String("floating"), floating);
    return obj;
}

ScrollScreenState ScrollScreenState::fromJson(const QJsonObject& obj)
{
    ScrollScreenState state;
    state.m_screenId = obj.value(QLatin1String("screenId")).toString();

    // A window id must be unique across the whole strip and the floating
    // set — duplicates from malformed JSON would corrupt locateWindow(),
    // removeWindow(), focusWindow() and the window counts. Drop any repeat.
    QSet<QString> seen;
    const QJsonArray columns = obj.value(QLatin1String("columns")).toArray();
    for (const QJsonValue& value : columns) {
        Column column = Column::fromJson(value.toObject());
        for (const QString& id : column.windowIds()) {
            if (seen.contains(id)) {
                column.removeWindow(id);
            } else {
                seen.insert(id);
            }
        }
        if (!column.isEmpty()) {
            state.m_columns.append(std::move(column));
        }
    }
    state.m_scrollX = obj.value(QLatin1String("scrollX")).toDouble(0.0);
    state.m_activeColumnIndex = obj.value(QLatin1String("activeColumnIndex")).toInt(-1);
    state.clampActiveColumnIndex();

    const QJsonArray floating = obj.value(QLatin1String("floating")).toArray();
    for (const QJsonValue& value : floating) {
        const QString id = value.toString();
        if (!id.isEmpty() && !seen.contains(id)) {
            state.m_floatingWindows.insert(id);
            seen.insert(id);
        }
    }
    return state;
}

std::pair<int, int> ScrollScreenState::locateWindow(const QString& windowId) const
{
    for (int columnIndex = 0; columnIndex < m_columns.size(); ++columnIndex) {
        const int tileIndex = m_columns.at(columnIndex).indexOfWindow(windowId);
        if (tileIndex >= 0) {
            return {columnIndex, tileIndex};
        }
    }
    return {-1, -1};
}

void ScrollScreenState::clampActiveColumnIndex()
{
    if (m_columns.isEmpty()) {
        m_activeColumnIndex = -1;
    } else {
        m_activeColumnIndex = qBound(0, m_activeColumnIndex, columnCount() - 1);
    }
}

void ScrollScreenState::refocus(const QString& preferredWindowId)
{
    if (!preferredWindowId.isEmpty()) {
        const auto [columnIndex, tileIndex] = locateWindow(preferredWindowId);
        if (columnIndex >= 0) {
            m_activeColumnIndex = columnIndex;
            m_columns[columnIndex].setActiveTileIndex(tileIndex);
            return;
        }
    }
    clampActiveColumnIndex();
}

QString ScrollScreenState::firstVisibleWindowId() const
{
    for (const Column& column : m_columns) {
        for (const Tile& tile : column.tiles()) {
            if (!tile.minimized) {
                return tile.windowId;
            }
        }
    }
    return QString();
}

} // namespace PhosphorScrollEngine
