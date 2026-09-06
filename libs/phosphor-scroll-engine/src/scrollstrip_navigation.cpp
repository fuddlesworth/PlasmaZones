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
    const int oldViewOffset = viewOffsetFor(params);
    m_activeColumnIdx = target;
    clampActiveIndices();
    reanchorAfterFocusChange(prevIdx, oldViewOffset, params);
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

bool ScrollStrip::focusTileByOrdinal(int ordinal)
{
    Column* col = activeColumnMutable();
    if (!col || ordinal < 1) {
        return false;
    }
    // Ordinals count the FOCUSABLE tiles, not the raw tile slots: a minimized
    // tile draws no tab (ResolvedTile::tabCount excludes them, and the sibling
    // walks refuse to land on one), so counting slots would make the visible
    // tabs and the numbers the user presses disagree the moment a tile in the
    // column is minimized.
    int seen = 0;
    for (int i = 0; i < col->tiles.size(); ++i) {
        if (col->tiles.at(i).minimized) {
            continue;
        }
        if (++seen == ordinal) {
            // Compared against activeTileIdx rather than activeWindowId(),
            // matching focusAdjacentTile and focusTileAtEnd. The two differ
            // only when activeTileIdx points at a MINIMIZED tile, which
            // production never produces (the compositor reports a minimize as
            // a float toggle, so the window leaves the column instead). The
            // family stays consistent rather than one verb diverging here.
            if (i == col->activeTileIdx) {
                return false;
            }
            col->activeTileIdx = i;
            return true;
        }
    }
    return false;
}

bool ScrollStrip::focusWindow(const QString& windowId, const ScrollLayoutParams& params)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    const int tileIdx = col.indexOfWindow(windowId);
    // A MINIMIZED tile must not become the column's active tile — the
    // sibling tile-focus walks (focusAdjacentTile, focusTileAtEnd) skip
    // minimized tiles for exactly the direct-operand hazard the
    // activeWindowId() contract documents. The column focus still moves so
    // the caller's column-level intent lands.
    const bool tileFocusable = tileIdx >= 0 && !col.tiles.at(tileIdx).minimized;
    if (colIdx == m_activeColumnIdx && (!tileFocusable || col.activeTileIdx == tileIdx)) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewOffset = viewOffsetFor(params);
    if (tileFocusable) {
        col.activeTileIdx = tileIdx;
    }
    m_activeColumnIdx = colIdx;
    // Re-anchor only when the active COLUMN changed. A same-column tile focus
    // moves no strip geometry (columnExtentPx folds in every non-minimized
    // tile regardless of activeTileIdx), and running the re-anchor anyway
    // clamps away the deliberately-unclamped anchors centerActiveColumn /
    // centerVisibleColumns leave behind — the applyLayout entry point guards
    // this exact hazard in updateViewForFocus, and the sibling tile-focus
    // verbs never touch the anchor at all.
    if (colIdx != prevIdx) {
        reanchorAfterFocusChange(prevIdx, oldViewOffset, params);
    }
    return true;
}

} // namespace PhosphorScrollEngine
