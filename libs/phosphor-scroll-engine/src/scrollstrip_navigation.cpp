// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

namespace PhosphorScrollEngine {

bool ScrollStrip::focusColumn(int columnIndex, const ScrollLayoutParams& params)
{
    if (m_columns.isEmpty()) {
        return false;
    }
    const int target = qBound(0, columnIndex, m_columns.size() - 1);
    if (target == m_activeColumnIdx) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    m_activeColumnIdx = target;
    clampActiveIndices();
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::focusAdjacentColumn(int delta, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0 || (delta != -1 && delta != 1)) {
        return false;
    }
    // Skip fully-minimized columns — they occupy no strip width and cannot
    // meaningfully take focus.
    int target = m_activeColumnIdx + delta;
    while (target >= 0 && target < m_columns.size() && m_columns.at(target).isFullyMinimized()) {
        target += delta;
    }
    if (target < 0 || target >= m_columns.size()) {
        return false;
    }
    return focusColumn(target, params);
}

bool ScrollStrip::focusFirstColumn(const ScrollLayoutParams& params)
{
    for (int i = 0; i < m_columns.size(); ++i) {
        if (!m_columns.at(i).isFullyMinimized()) {
            return focusColumn(i, params);
        }
    }
    return false;
}

bool ScrollStrip::focusLastColumn(const ScrollLayoutParams& params)
{
    for (int i = m_columns.size() - 1; i >= 0; --i) {
        if (!m_columns.at(i).isFullyMinimized()) {
            return focusColumn(i, params);
        }
    }
    return false;
}

bool ScrollStrip::focusAdjacentTile(int delta)
{
    Column* col = activeColumnMutable();
    if (!col || (delta != -1 && delta != 1)) {
        return false;
    }
    int target = col->activeTileIdx + delta;
    while (target >= 0 && target < col->tiles.size() && col->tiles.at(target).minimized) {
        target += delta;
    }
    if (target < 0 || target >= col->tiles.size() || target == col->activeTileIdx) {
        return false;
    }
    col->activeTileIdx = target;
    return true;
}

bool ScrollStrip::focusTileAtEnd(bool last)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    // Same minimized-skip walk as focusAdjacentTile, seeded at the end.
    const int step = last ? -1 : 1;
    int target = last ? col->tiles.size() - 1 : 0;
    while (target >= 0 && target < col->tiles.size() && col->tiles.at(target).minimized) {
        target += step;
    }
    if (target < 0 || target >= col->tiles.size() || target == col->activeTileIdx) {
        return false;
    }
    col->activeTileIdx = target;
    return true;
}

bool ScrollStrip::focusWindow(const QString& windowId, const ScrollLayoutParams& params)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    const int tileIdx = col.indexOfWindow(windowId);
    if (colIdx == m_activeColumnIdx && col.activeTileIdx == tileIdx) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    col.activeTileIdx = tileIdx;
    m_activeColumnIdx = colIdx;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

} // namespace PhosphorScrollEngine
