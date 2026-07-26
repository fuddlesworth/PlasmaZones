// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

namespace PhosphorScrollEngine {

// ── Introspection ───────────────────────────────────────────────────────────

const Column* ScrollStrip::activeColumn() const
{
    if (m_activeColumnIdx < 0 || m_activeColumnIdx >= m_columns.size()) {
        return nullptr;
    }
    return &m_columns.at(m_activeColumnIdx);
}

Column* ScrollStrip::activeColumnMutable()
{
    if (m_activeColumnIdx < 0 || m_activeColumnIdx >= m_columns.size()) {
        return nullptr;
    }
    return &m_columns[m_activeColumnIdx];
}

Tile* ScrollStrip::activeTileMutable()
{
    Column* col = activeColumnMutable();
    if (!col || col->activeTileIdx < 0 || col->activeTileIdx >= col->tiles.size()) {
        return nullptr;
    }
    return &col->tiles[col->activeTileIdx];
}

QString ScrollStrip::activeWindowId() const
{
    const Column* col = activeColumn();
    if (!col || col->activeTileIdx < 0 || col->activeTileIdx >= col->tiles.size()) {
        return {};
    }
    return col->tiles.at(col->activeTileIdx).windowId;
}

int ScrollStrip::columnOfWindow(const QString& windowId) const
{
    for (int i = 0; i < m_columns.size(); ++i) {
        if (m_columns.at(i).indexOfWindow(windowId) != -1) {
            return i;
        }
    }
    return -1;
}

QStringList ScrollStrip::windowsInOrder() const
{
    QStringList out;
    for (const Column& col : m_columns) {
        for (const Tile& tile : col.tiles) {
            out.append(tile.windowId);
        }
    }
    return out;
}

int ScrollStrip::windowCount() const
{
    int count = 0;
    for (const Column& col : m_columns) {
        count += col.tiles.size();
    }
    return count;
}

void ScrollStrip::clampActiveIndices()
{
    if (m_columns.isEmpty()) {
        m_activeColumnIdx = -1;
        return;
    }
    m_activeColumnIdx = qBound(0, m_activeColumnIdx, m_columns.size() - 1);
    Column& col = m_columns[m_activeColumnIdx];
    col.activeTileIdx = col.tiles.isEmpty() ? 0 : qBound(0, col.activeTileIdx, col.tiles.size() - 1);
}

// ── Open / close / minimize ─────────────────────────────────────────────────

bool ScrollStrip::insertWindow(const QString& windowId, const ColumnWidth& width, ColumnDisplay display,
                               const ScrollLayoutParams& params, int minWidth, int minHeight)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);

    Column col;
    col.width = width;
    col.display = display;
    Tile tile;
    tile.windowId = windowId;
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    col.tiles.append(tile);

    const int insertAt = m_columns.isEmpty() ? 0 : m_activeColumnIdx + 1;
    m_columns.insert(insertAt, col);
    if (m_preMaximizeColumnIdx >= insertAt) {
        ++m_preMaximizeColumnIdx;
    }
    m_activeColumnIdx = insertAt;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::insertWindowIntoActiveColumn(const QString& windowId, const ColumnWidth& width, ColumnDisplay display,
                                               const ScrollLayoutParams& params, int minWidth, int minHeight)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return false;
    }
    Column* col = activeColumnMutable();
    if (!col) {
        return insertWindow(windowId, width, display, params, minWidth, minHeight);
    }
    Tile tile;
    tile.windowId = windowId;
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    col->tiles.append(tile);
    col->activeTileIdx = col->tiles.size() - 1;
    // The arrival joins an EXISTING column: the column's width intent is
    // the host's and deliberately stays (an open-rule width override would
    // resize every sibling in the stack), but a display override (tabbed)
    // is column-level presentation the rule can legitimately set.
    if (display != col->display) {
        col->display = display;
    }
    Q_UNUSED(width)
    return true;
}

bool ScrollStrip::insertWindowIntoColumnAt(int columnIndex, int tileIndex, const QString& windowId, int minWidth,
                                           int minHeight)
{
    if (windowId.isEmpty() || containsWindow(windowId) || columnIndex < 0 || columnIndex >= m_columns.size()) {
        return false;
    }
    Column& col = m_columns[columnIndex];
    Tile tile;
    tile.windowId = windowId;
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    const int at = qBound(0, tileIndex, col.tiles.size());
    col.tiles.insert(at, tile);
    if (col.activeTileIdx >= at) {
        ++col.activeTileIdx;
    }
    col.activeTileIdx = at;
    m_activeColumnIdx = columnIndex;
    return true;
}

bool ScrollStrip::insertWindowAt(int columnIndex, const QString& windowId, const ColumnWidth& width,
                                 ColumnDisplay display)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return false;
    }
    Column col;
    col.width = width;
    col.display = display;
    Tile tile;
    tile.windowId = windowId;
    col.tiles.append(tile);

    const int insertAt = qBound(0, columnIndex, m_columns.size());
    m_columns.insert(insertAt, col);
    if (m_activeColumnIdx >= insertAt) {
        ++m_activeColumnIdx;
    }
    if (m_preMaximizeColumnIdx >= insertAt) {
        ++m_preMaximizeColumnIdx;
    }
    if (m_activeColumnIdx < 0) {
        m_activeColumnIdx = insertAt;
    }
    return true;
}

void ScrollStrip::removeColumnAt(int columnIndex)
{
    m_columns.removeAt(columnIndex);
    if (m_preMaximizeColumnIdx == columnIndex) {
        m_preMaximizeColumnIdx = -1;
    } else if (m_preMaximizeColumnIdx > columnIndex) {
        --m_preMaximizeColumnIdx;
    }
}

bool ScrollStrip::removeWindowInternal(const QString& windowId, const ScrollLayoutParams& params, bool refocus)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    const int oldViewX = viewXFor(params);
    int prevIdx = m_activeColumnIdx;

    Column& col = m_columns[colIdx];
    const int tileIdx = col.indexOfWindow(windowId);
    col.tiles.removeAt(tileIdx);
    if (col.activeTileIdx > tileIdx || col.activeTileIdx >= col.tiles.size()) {
        col.activeTileIdx = qMax(0, col.activeTileIdx - 1);
    }

    bool columnClosed = false;
    if (col.tiles.isEmpty()) {
        removeColumnAt(colIdx);
        columnClosed = true;
    }

    if (m_columns.isEmpty()) {
        m_activeColumnIdx = -1;
        m_viewAnchor = 0;
        return true;
    }

    // Index fixups: the active column keeps identity when it survived.
    if (columnClosed) {
        // prevIdx is consumed by reanchorAfterFocusChange AFTER the removal
        // shifted indices — adjust it too, or OnOverflow reads a DIFFERENT
        // column's width as prevW and the entering-edge test can invert.
        if (prevIdx > colIdx) {
            --prevIdx;
        } else if (prevIdx == colIdx) {
            prevIdx = -1; // the previously-active column no longer exists
        }
        if (m_activeColumnIdx > colIdx) {
            --m_activeColumnIdx;
        } else if (m_activeColumnIdx == colIdx) {
            // The active column vanished: focus the column that took its
            // place (the right neighbour), or the new last column when the
            // closed one was rightmost.
            m_activeColumnIdx = qMin(colIdx, m_columns.size() - 1);
        }
    }
    clampActiveIndices();

    // Keep survivors visually stationary: the anchor is relative to the
    // active column, so re-derive it from the pre-removal viewX. Without
    // refocus (take/transfer path) the same math applies — the caller wants
    // zero visual churn while it re-homes the window. The one hard rule is
    // the strip's left edge: never expose space left of the first column.
    // DELIBERATE asymmetry with keepOrRecenterAnchor (minimize-collapse /
    // consume), which also clamps the RIGHT edge: on a removal the user
    // just lost a window, and keeping the survivors pixel-stationary beats
    // reclaiming right-edge dead space — the next focus change reclaims it.
    const int stripX = columnStripX(m_activeColumnIdx, params);
    int anchor = stripX - oldViewX;
    if (stripX - anchor < 0) {
        anchor = stripX;
    }
    m_viewAnchor = anchor;

    if (refocus) {
        const int workW = params.workArea.width();
        const int colW = columnWidthPx(m_columns.at(m_activeColumnIdx), params);
        const bool centerLone = params.alwaysCenterSingleColumn && m_columns.size() == 1;
        if (centerLone || params.centerFocusedColumn == CenterFocusedColumn::Always) {
            m_viewAnchor = centeredAnchorFor(m_activeColumnIdx, params);
        } else if (m_viewAnchor < 0 || m_viewAnchor + colW > workW) {
            // The newly-focused column is (partly) out of view — scroll it
            // in per the centering policy. A fully visible column stays put
            // even if that leaves empty strip on the right; the next focus
            // change reclaims it.
            reanchorAfterFocusChange(prevIdx, oldViewX, params);
        }
    }
    return true;
}

bool ScrollStrip::removeWindow(const QString& windowId, const ScrollLayoutParams& params)
{
    return removeWindowInternal(windowId, params, true);
}

bool ScrollStrip::takeWindow(const QString& windowId, const ScrollLayoutParams& params)
{
    return removeWindowInternal(windowId, params, false);
}

bool ScrollStrip::setWindowMinimized(const QString& windowId, bool minimized, const ScrollLayoutParams& params)
{
    // NOTE: PlasmaZones' own daemon does not call this — the compositor
    // reports minimize as a float toggle (see ScrollEngine.h, "minimize
    // machinery"), so production minimize rides FloatRestore. This is the
    // strip-level model for embedders that deliver a real minimize signal;
    // the relayout/focus/anchor machinery it drives is pinned by the
    // library's own tests.
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    const int tileIdx = col.indexOfWindow(windowId);
    Tile& tile = col.tiles[tileIdx];
    if (tile.minimized == minimized) {
        return false;
    }
    const int oldViewX = viewXFor(params);
    tile.minimized = minimized;
    if (minimized && col.activeTileIdx == tileIdx) {
        // Prefer the NEAREST visible sibling as the column's active tile
        // (ties break downward), so focus does not jump to the top of a
        // tall stack when a middle tile minimizes.
        for (int dist = 1; dist < col.tiles.size(); ++dist) {
            const int below = tileIdx + dist;
            const int above = tileIdx - dist;
            if (below < col.tiles.size() && !col.tiles.at(below).minimized) {
                col.activeTileIdx = below;
                break;
            }
            if (above >= 0 && !col.tiles.at(above).minimized) {
                col.activeTileIdx = above;
                break;
            }
        }
    }
    if (!minimized) {
        col.activeTileIdx = tileIdx;
    }
    // A column collapsing to / expanding from fully-minimized shifts strip
    // positions; keep the view where it was (clamped so the collapse can
    // never leave dead space beyond the strip's right end) unless the
    // centering policy re-centers the focused column.
    m_viewAnchor = keepOrRecenterAnchor(oldViewX, params);
    return true;
}

bool ScrollStrip::isWindowMinimized(const QString& windowId) const
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    const Column& col = m_columns.at(colIdx);
    return col.tiles.at(col.indexOfWindow(windowId)).minimized;
}

QSize ScrollStrip::windowMinimumSize(const QString& windowId) const
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return QSize(0, 0);
    }
    const Column& col = m_columns.at(colIdx);
    const Tile& tile = col.tiles.at(col.indexOfWindow(windowId));
    return QSize(tile.minWidth, tile.minHeight);
}

bool ScrollStrip::setWindowMinimumSize(const QString& windowId, int minWidth, int minHeight)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    Tile& tile = col.tiles[col.indexOfWindow(windowId)];
    if (tile.minWidth == minWidth && tile.minHeight == minHeight) {
        return false;
    }
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    return true;
}

// ── Move / consume / expel ──────────────────────────────────────────────────

bool ScrollStrip::moveActiveColumn(int delta, const ScrollLayoutParams& params)
{
    const int target = m_activeColumnIdx + delta;
    if (m_activeColumnIdx < 0 || target < 0 || target >= m_columns.size()) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    m_columns.swapItemsAt(m_activeColumnIdx, target);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = target;
    } else if (m_preMaximizeColumnIdx == target) {
        m_preMaximizeColumnIdx = m_activeColumnIdx;
    }
    m_activeColumnIdx = target;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::moveActiveColumnTo(int target, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0 || target < 0 || target >= m_columns.size() || target == m_activeColumnIdx) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    m_columns.move(m_activeColumnIdx, target);
    // Pre-maximize slot follows the same element move.
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = target;
    } else if (m_preMaximizeColumnIdx >= 0) {
        if (m_activeColumnIdx < m_preMaximizeColumnIdx && m_preMaximizeColumnIdx <= target) {
            --m_preMaximizeColumnIdx;
        } else if (target <= m_preMaximizeColumnIdx && m_preMaximizeColumnIdx < m_activeColumnIdx) {
            ++m_preMaximizeColumnIdx;
        }
    }
    m_activeColumnIdx = target;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::moveActiveColumnToFirst(const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx <= 0) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    m_columns.move(m_activeColumnIdx, 0);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = 0;
    } else if (m_preMaximizeColumnIdx < m_activeColumnIdx && m_preMaximizeColumnIdx >= 0) {
        ++m_preMaximizeColumnIdx;
    }
    m_activeColumnIdx = 0;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::moveActiveColumnToLast(const ScrollLayoutParams& params)
{
    const int last = m_columns.size() - 1;
    if (m_activeColumnIdx < 0 || m_activeColumnIdx >= last) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    m_columns.move(m_activeColumnIdx, last);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = last;
    } else if (m_preMaximizeColumnIdx > m_activeColumnIdx) {
        --m_preMaximizeColumnIdx;
    }
    m_activeColumnIdx = last;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::moveActiveTile(int delta)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    const int target = col->activeTileIdx + delta;
    if (target < 0 || target >= col->tiles.size()) {
        return false;
    }
    col->tiles.swapItemsAt(col->activeTileIdx, target);
    col->activeTileIdx = target;
    return true;
}

bool ScrollStrip::consumeWindowIntoColumn(const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0 || m_activeColumnIdx + 1 >= m_columns.size()) {
        return false;
    }
    // A fully-minimized neighbour is invisible (every focus verb skips it);
    // pulling a hidden tile out of it would surprise. Match the focus
    // verbs and refuse.
    if (m_columns.at(m_activeColumnIdx + 1).isFullyMinimized()) {
        return false;
    }
    const int oldViewX = viewXFor(params);
    Column& source = m_columns[m_activeColumnIdx + 1];
    const int takeIdx = qBound(0, source.activeTileIdx, source.tiles.size() - 1);
    const Tile taken = source.tiles.takeAt(takeIdx);
    if (source.activeTileIdx >= source.tiles.size()) {
        source.activeTileIdx = qMax(0, source.tiles.size() - 1);
    }
    Column& dest = m_columns[m_activeColumnIdx];
    dest.tiles.append(taken);
    dest.activeTileIdx = dest.tiles.size() - 1;
    if (source.tiles.isEmpty()) {
        removeColumnAt(m_activeColumnIdx + 1);
    }
    // Consuming shifts everything right of the active column; the anchor is
    // active-relative so the focused column stays put by construction —
    // except under the Always/lone-column centering policy, where the
    // narrower strip re-centers it.
    m_viewAnchor = keepOrRecenterAnchor(oldViewX, params);
    return true;
}

bool ScrollStrip::expelWindowFromColumn(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || col->tiles.size() < 2) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);
    const Tile expelled = col->tiles.takeAt(col->activeTileIdx);
    if (col->activeTileIdx >= col->tiles.size()) {
        col->activeTileIdx = col->tiles.size() - 1;
    }
    Column newCol;
    newCol.width = col->width;
    newCol.display = ColumnDisplay::Normal;
    newCol.tiles.append(expelled);
    const int insertAt = m_activeColumnIdx + 1;
    m_columns.insert(insertAt, newCol);
    if (m_preMaximizeColumnIdx >= insertAt) {
        ++m_preMaximizeColumnIdx;
    }
    m_activeColumnIdx = insertAt;
    reanchorAfterFocusChange(prevIdx, oldViewX, params);
    return true;
}

bool ScrollStrip::consumeOrExpel(int delta, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || (delta != -1 && delta != 1)) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewX = viewXFor(params);

    if (col->tiles.size() > 1) {
        // Expel into a new column on the delta side.
        const Tile expelled = col->tiles.takeAt(col->activeTileIdx);
        if (col->activeTileIdx >= col->tiles.size()) {
            col->activeTileIdx = col->tiles.size() - 1;
        }
        Column newCol;
        newCol.width = col->width;
        newCol.tiles.append(expelled);
        const int insertAt = delta > 0 ? m_activeColumnIdx + 1 : m_activeColumnIdx;
        m_columns.insert(insertAt, newCol);
        if (m_preMaximizeColumnIdx >= insertAt) {
            ++m_preMaximizeColumnIdx;
        }
        m_activeColumnIdx = insertAt;
        reanchorAfterFocusChange(prevIdx, oldViewX, params);
        return true;
    }

    // Alone in its column: consume into the neighbour on the delta side.
    const int neighbourIdx = m_activeColumnIdx + delta;
    if (neighbourIdx < 0 || neighbourIdx >= m_columns.size()) {
        return false;
    }
    const Tile taken = col->tiles.takeAt(0);
    removeColumnAt(m_activeColumnIdx);
    const int destIdx = delta > 0 ? m_activeColumnIdx : m_activeColumnIdx - 1;
    Column& dest = m_columns[destIdx];
    dest.tiles.append(taken);
    dest.activeTileIdx = dest.tiles.size() - 1;
    m_activeColumnIdx = destIdx;
    // The previously-active column ceased to exist and later indices
    // shifted, so prevIdx no longer names it — passing it through would
    // make the OnOverflow test read a DIFFERENT column's width as prevW
    // (same hazard removeWindowInternal documents).
    reanchorAfterFocusChange(-1, oldViewX, params);
    return true;
}

// ── Display ─────────────────────────────────────────────────────────────────

bool ScrollStrip::toggleActiveColumnTabbed()
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    col->display = (col->display == ColumnDisplay::Tabbed) ? ColumnDisplay::Normal : ColumnDisplay::Tabbed;
    return true;
}

} // namespace PhosphorScrollEngine
