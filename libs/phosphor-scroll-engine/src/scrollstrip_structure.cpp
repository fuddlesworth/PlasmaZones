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
    // Never name a minimized tile. Relayout's tabbed branch falls back to the
    // first VISIBLE tile when the stored index points at a minimized one, so
    // returning the minimized id here let applyLayout(focusWindowAfter) ask
    // the compositor to activate a window that was never laid out. Mirror the
    // relayout fallback so the two notions of "active tile" cannot disagree.
    if (!col->tiles.at(col->activeTileIdx).minimized) {
        return col->tiles.at(col->activeTileIdx).windowId;
    }
    for (const Tile& tile : col->tiles) {
        if (!tile.minimized) {
            return tile.windowId;
        }
    }
    return {};
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
                               const ScrollLayoutParams& params, int minWidth, int minHeight, ScrollInsertPosition pos)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewOffset = viewOffsetFor(params);

    Column col;
    col.width = width;
    col.display = display;
    Tile tile;
    tile.windowId = windowId;
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    tile.height = params.defaultWindowHeight;
    col.tiles.append(tile);

    int insertAt = m_columns.isEmpty() ? 0 : m_activeColumnIdx + 1;
    if (!m_columns.isEmpty()) {
        switch (pos) {
        case ScrollInsertPosition::First:
            insertAt = 0;
            break;
        case ScrollInsertPosition::Last:
            insertAt = m_columns.size();
            break;
        case ScrollInsertPosition::LeftOfActive:
            insertAt = m_activeColumnIdx;
            break;
        case ScrollInsertPosition::RightOfActive:
        case ScrollInsertPosition::IntoActiveColumn: // engine-routed; degrade
            insertAt = m_activeColumnIdx + 1;
            break;
        }
    }
    // Cheap hardening only: the -1 active-index sentinel exists solely under
    // isEmpty(), so a non-empty strip implies insertAt is already in range.
    insertAt = qBound(0, insertAt, int(m_columns.size()));
    m_columns.insert(insertAt, col);
    if (m_preMaximizeColumnIdx >= insertAt) {
        ++m_preMaximizeColumnIdx;
    }
    // prevIdx was captured BEFORE the insert shifted the columns at and past
    // insertAt (m_preMaximizeColumnIdx is adjusted for the same reason just
    // above). Left stale, the OnOverflow arm of the reanchor reads the wrong
    // column's width as prevW — or, for a First/LeftOfActive insert, reads
    // the column that just arrived and degrades OnOverflow to Never.
    int shiftedPrevIdx = prevIdx;
    if (shiftedPrevIdx >= insertAt) {
        ++shiftedPrevIdx;
    }
    m_activeColumnIdx = insertAt;
    reanchorAfterFocusChange(shiftedPrevIdx, oldViewOffset, params);
    return true;
}

bool ScrollStrip::insertWindowIntoActiveColumn(const QString& windowId, const ColumnWidth& width,
                                               std::optional<ColumnDisplay> displayOverride,
                                               const ScrollLayoutParams& params, int minWidth, int minHeight)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return false;
    }
    Column* col = activeColumnMutable();
    if (!col) {
        return insertWindow(windowId, width, displayOverride.value_or(ColumnDisplay::Normal), params, minWidth,
                            minHeight);
    }
    Tile tile;
    tile.windowId = windowId;
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    tile.height = params.defaultWindowHeight;
    col->tiles.append(tile);
    col->activeTileIdx = col->tiles.size() - 1;
    // The arrival joins an EXISTING column: the column's width intent is
    // the host's and deliberately stays (an open-rule width override would
    // resize every sibling in the stack). Display is column-level
    // presentation an EXPLICIT openTabbed rule can legitimately set; a
    // disengaged override (plain consume-open) keeps the host's display —
    // overwriting with the config default would silently un-tab a column
    // the user toggled.
    if (displayOverride && *displayOverride != col->display) {
        col->display = *displayOverride;
    }
    // No re-anchor and no re-clamp, unlike every sibling insert verb: the
    // arrival joins the column that is ALREADY active, so no column index
    // shifts and the active column does not change. The strip's total width
    // is USUALLY unchanged — under respectMinimumSize a new tile's declared
    // minimum can widen the host column, and updateViewForFocus backstops
    // the view for that case. The anchor is active-relative, so it still
    // means exactly what it did before the append.
    Q_UNUSED(width)
    return true;
}

bool ScrollStrip::insertWindowIntoColumnAt(int columnIndex, int tileIndex, const QString& windowId,
                                           const ScrollLayoutParams& params, int minWidth, int minHeight)
{
    if (windowId.isEmpty() || containsWindow(windowId) || columnIndex < 0 || columnIndex >= m_columns.size()) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewOffset = viewOffsetFor(params);
    Column& col = m_columns[columnIndex];
    Tile tile;
    tile.windowId = windowId;
    tile.minWidth = minWidth;
    tile.minHeight = minHeight;
    // Seeded like every other tile-creating verb; the restore callers that
    // have a remembered intent overwrite it via setWindowHeightIntent.
    tile.height = params.defaultWindowHeight;
    const int at = qBound(0, tileIndex, col.tiles.size());
    col.tiles.insert(at, tile);
    col.activeTileIdx = at;
    m_activeColumnIdx = columnIndex;
    // The anchor is active-relative: reassigning the active column without
    // re-deriving it would reinterpret the stored anchor against a
    // different column's stripX (a viewport jump on unfloat of a stacked
    // tile — the caller's focusWindow no-ops because the state below is
    // already what it would install).
    reanchorAfterFocusChange(prevIdx, oldViewOffset, params);
    return true;
}

bool ScrollStrip::insertWindowAt(int columnIndex, const QString& windowId, const ColumnWidth& width,
                                 ColumnDisplay display, const ScrollLayoutParams& params)
{
    if (windowId.isEmpty() || containsWindow(windowId)) {
        return false;
    }
    Column col;
    col.width = width;
    col.display = display;
    Tile tile;
    tile.windowId = windowId;
    // Restore callers overwrite this via setWindowHeightIntent; a fresh
    // positional insert (cross-mode handoff landing slot) takes the default.
    tile.height = params.defaultWindowHeight;
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
    // The anchor is active-relative, so the active column stays visually
    // stationary through positional inserts — but it must be re-clamped:
    // a mode-transition seed inserts every earlier window to the LEFT of
    // the first-adopted (active) column, and the unclamped anchor left the
    // active column pinned at the viewport's left edge with every other
    // column parked off-screen and dead space on the right (the "toggle
    // into scrolling shows one window" bug). Clamping is a no-op while the
    // strip is narrower than the viewport.
    if (params.workArea.isValid()) {
        m_viewAnchor = clampedAnchor(m_viewAnchor, params);
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
    const int oldViewOffset = viewOffsetFor(params);
    int prevIdx = m_activeColumnIdx;
    // Captured in PRE-removal indexing, where colIdx and the active index
    // are still comparable — the side decides the anchor policy below.
    const bool removedLeftOfActive = colIdx < m_activeColumnIdx;

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

    // Anchor policy splits on which SIDE of the active column the removal
    // happened, matching niri's remove_column_by_idx ("A column to the left
    // was removed; preserve the current position"):
    //
    // LEFT of active — keep the anchor. The anchor is the active column's
    // on-screen offset, so keeping it holds the column the user is looking
    // at pixel-stationary while the left-side survivors slide right to
    // close the gap. Re-deriving from the old viewOffset here (the previous
    // behaviour) slid the WHOLE visible strip left instead, active column
    // included.
    //
    // AT or RIGHT of active — re-derive from the pre-removal viewOffset. The
    // active-and-left strip coordinates are unchanged there, so this keeps
    // every surviving on-screen column stationary and the gap closes from
    // the right. Without refocus (take/transfer path) the same math
    // applies — the caller wants zero visual churn while it re-homes the
    // window.
    //
    // The one hard rule for BOTH sides is the strip's left edge: never
    // expose space left of the first column. DELIBERATE asymmetry with
    // keepOrRecenterAnchor (minimize-collapse / consume), which also clamps
    // the RIGHT edge: on a removal the user just lost a window, and keeping
    // the view stationary beats reclaiming right-edge dead space. The next
    // focus change reclaims it; the engine's applyLayout does NOT
    // (updateViewForFocus leaves a fully-visible column's anchor alone
    // precisely to preserve this).
    // Degenerate-area guard, same rationale as clampedAnchor's
    // (scrollstrip_relayout.cpp): every width resolves to 0 here, so the
    // left-edge rule below collapses any positive anchor to 0 — and the
    // anchor is PERSISTED. The removal itself stands; only the anchor
    // bookkeeping is skipped, and the first relayout against a real area
    // re-clamps whatever survived.
    if (params.workArea.width() > 0) {
        const int stripX = columnStripPos(m_activeColumnIdx, params);
        int anchor = removedLeftOfActive ? m_viewAnchor : stripX - oldViewOffset;
        if (stripX - anchor < 0) {
            anchor = stripX;
        }
        m_viewAnchor = anchor;
    }

    if (refocus) {
        const int workW = params.workArea.width();
        const int colW = columnExtentPx(m_columns.at(m_activeColumnIdx), params);
        const bool centerLone = params.alwaysCenterSingleColumn && m_columns.size() == 1;
        if (centerLone || params.centerFocusedColumn == CenterFocusedColumn::Always) {
            m_viewAnchor = centeredAnchorFor(m_activeColumnIdx, params);
        } else if (m_viewAnchor < 0 || m_viewAnchor + colW > workW) {
            // The newly-focused column is (partly) out of view — scroll it
            // in per the centering policy. A fully visible column stays put
            // even if that leaves empty strip on the right; the next focus
            // change reclaims it.
            reanchorAfterFocusChange(prevIdx, oldViewOffset, params);
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
    //
    // Constraint for those embedders: a minimized tile is dropped from
    // relayout()'s output entirely, so the engine emits no rect for it and
    // ScrollEngine::m_lastAppliedRect (the lastManagedRect float-back poison
    // guard) keeps answering with the tile's PRE-minimize rect for as long as
    // it stays minimized. That retention is wanted — a minimized window's
    // frame is still the tile rect, which is exactly what the guard must
    // recognise — but it also means the engine's emit-on-change gate sees no
    // movement when a restore resolves back to that same rect, and issues no
    // geometry for it.
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
    const int oldViewOffset = viewOffsetFor(params);
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
        // The restored tile takes its COLUMN's active slot back — EXCEPT in
        // a TABBED column whose active tile is visible: there the active
        // slot IS the shown window, and stealing it would swap out what the
        // user is looking at with no activation emitted. In a Normal column
        // every tile is visible, so re-taking the slot is the documented
        // restore contract. The strip's active column is deliberately
        // untouched either way — unminimizing into a background column must
        // not steal focus from the column the user is working in (embedders
        // call focusWindow).
        const bool tabbedShowingVisible = col.display == ColumnDisplay::Tabbed && col.activeTileIdx >= 0
            && col.activeTileIdx < col.tiles.size() && !col.tiles.at(col.activeTileIdx).minimized;
        if (!tabbedShowingVisible) {
            col.activeTileIdx = tileIdx;
        }
    }
    // A column collapsing to / expanding from fully-minimized shifts strip
    // positions; keep the view where it was (clamped so the collapse can
    // never leave dead space beyond the strip's right end) unless the
    // centering policy re-centers the focused column.
    m_viewAnchor = keepOrRecenterAnchor(oldViewOffset, params);
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
    // The unguarded .at(indexOfWindow(...)) here (and in the minimized /
    // set-min-size siblings) is safe BY CONSTRUCTION: columnOfWindow returns
    // colIdx only when this exact column's indexOfWindow answered >= 0, both
    // are pure const scans over the same unmutated container, so the
    // re-derived index cannot be -1. A runtime guard would be dead code.
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
    const int oldViewOffset = viewOffsetFor(params);
    m_columns.swapItemsAt(m_activeColumnIdx, target);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = target;
    } else if (m_preMaximizeColumnIdx == target) {
        m_preMaximizeColumnIdx = m_activeColumnIdx;
    }
    m_activeColumnIdx = target;
    // prevIdx = -1: the move shifted indices, so the saved index names a
    // DIFFERENT column now (the OnOverflow prevW hazard removeWindowInternal
    // documents) — and a move does not change WHICH window is focused, so
    // no entering-edge/overflow test should fire; keep the view stationary.
    reanchorAfterFocusChange(-1, oldViewOffset, params);
    return true;
}

bool ScrollStrip::moveActiveColumnTo(int target, const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx < 0 || target < 0 || target >= m_columns.size() || target == m_activeColumnIdx) {
        return false;
    }
    const int oldViewOffset = viewOffsetFor(params);
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
    // prevIdx = -1: same rationale as moveActiveColumn.
    reanchorAfterFocusChange(-1, oldViewOffset, params);
    return true;
}

bool ScrollStrip::moveActiveColumnToFirst(const ScrollLayoutParams& params)
{
    if (m_activeColumnIdx <= 0) {
        return false;
    }
    const int oldViewOffset = viewOffsetFor(params);
    m_columns.move(m_activeColumnIdx, 0);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = 0;
    } else if (m_preMaximizeColumnIdx < m_activeColumnIdx && m_preMaximizeColumnIdx >= 0) {
        ++m_preMaximizeColumnIdx;
    }
    m_activeColumnIdx = 0;
    // prevIdx = -1: same rationale as moveActiveColumn.
    reanchorAfterFocusChange(-1, oldViewOffset, params);
    return true;
}

bool ScrollStrip::moveActiveColumnToLast(const ScrollLayoutParams& params)
{
    const int last = m_columns.size() - 1;
    if (m_activeColumnIdx < 0 || m_activeColumnIdx >= last) {
        return false;
    }
    const int oldViewOffset = viewOffsetFor(params);
    m_columns.move(m_activeColumnIdx, last);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = last;
    } else if (m_preMaximizeColumnIdx > m_activeColumnIdx) {
        --m_preMaximizeColumnIdx;
    }
    m_activeColumnIdx = last;
    // prevIdx = -1: same rationale as moveActiveColumn.
    reanchorAfterFocusChange(-1, oldViewOffset, params);
    return true;
}

bool ScrollStrip::moveActiveTile(int delta)
{
    Column* col = activeColumnMutable();
    if (!col || delta == 0) {
        return false;
    }
    // Skip minimized slots the way focusAdjacentTile does. Relayout drops
    // minimized tiles entirely, so swapping across one moved nothing on
    // screen while the verb still reported success and fired a success OSD
    // plus a placementChanged.
    const int step = delta > 0 ? 1 : -1;
    int target = col->activeTileIdx;
    for (int remaining = qAbs(delta); remaining > 0; --remaining) {
        int next = target + step;
        while (next >= 0 && next < col->tiles.size() && col->tiles.at(next).minimized) {
            next += step;
        }
        if (next < 0 || next >= col->tiles.size()) {
            return false;
        }
        target = next;
    }
    col->tiles.move(col->activeTileIdx, target);
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
    const int oldViewOffset = viewOffsetFor(params);
    Column& source = m_columns[m_activeColumnIdx + 1];
    int takeIdx = qBound(0, source.activeTileIdx, source.tiles.size() - 1);
    // The neighbour is not fully minimized (guarded above), but its ACTIVE
    // tile still can be — taking that one would pull an invisible window into
    // this column and make it the active tile. Walk to the nearest visible
    // tile instead. Ties break DOWNWARD, matching setWindowMinimized's
    // documented policy: the two are the same "nearest visible tile" question
    // and must not answer it differently. (consumeOrExpel does NOT walk at
    // all in this situation — it refuses outright.)
    if (source.tiles.at(takeIdx).minimized) {
        int visible = -1;
        for (int off = 1; off < source.tiles.size() && visible < 0; ++off) {
            for (const int cand : {takeIdx + off, takeIdx - off}) {
                if (cand >= 0 && cand < source.tiles.size() && !source.tiles.at(cand).minimized) {
                    visible = cand;
                    break;
                }
            }
        }
        if (visible < 0) {
            return false;
        }
        takeIdx = visible;
    }
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
    m_viewAnchor = keepOrRecenterAnchor(oldViewOffset, params);
    return true;
}

bool ScrollStrip::expelWindowFromColumn(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || col->tiles.size() < 2) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewOffset = viewOffsetFor(params);
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
    reanchorAfterFocusChange(prevIdx, oldViewOffset, params);
    return true;
}

bool ScrollStrip::consumeOrExpel(int delta, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || (delta != -1 && delta != 1)) {
        return false;
    }
    const int prevIdx = m_activeColumnIdx;
    const int oldViewOffset = viewOffsetFor(params);

    if (col->tiles.size() > 1) {
        // Expel into a new column on the delta side.
        const Tile expelled = col->tiles.takeAt(col->activeTileIdx);
        if (col->activeTileIdx >= col->tiles.size()) {
            col->activeTileIdx = col->tiles.size() - 1;
        }
        Column newCol;
        newCol.width = col->width;
        // Explicit, matching the consume twin: a single expelled tile is a
        // Normal column regardless of the host's (possibly Tabbed) display.
        newCol.display = ColumnDisplay::Normal;
        newCol.tiles.append(expelled);
        const int insertAt = delta > 0 ? m_activeColumnIdx + 1 : m_activeColumnIdx;
        m_columns.insert(insertAt, newCol);
        if (m_preMaximizeColumnIdx >= insertAt) {
            ++m_preMaximizeColumnIdx;
        }
        // Same stale-prevIdx shift insertWindow carries: expelling LEFT
        // (delta < 0) inserts AT the previously-active index, so an unshifted
        // prevIdx names the column that just arrived — prevIdx == active,
        // which makes the OnOverflow test dead and silently degrades it to
        // Never.
        int shiftedPrevIdx = prevIdx;
        if (shiftedPrevIdx >= insertAt) {
            ++shiftedPrevIdx;
        }
        m_activeColumnIdx = insertAt;
        reanchorAfterFocusChange(shiftedPrevIdx, oldViewOffset, params);
        return true;
    }

    // Alone in its column: consume into the neighbour on the delta side.
    const int neighbourIdx = m_activeColumnIdx + delta;
    if (neighbourIdx < 0 || neighbourIdx >= m_columns.size()) {
        return false;
    }
    // Same refusal as consumeWindowIntoColumn: a fully-minimized column is
    // skipped by every focus verb, so consuming into one would make the
    // window vanish from view. The MOVED tile must be visible too —
    // appending a minimized tile to a visible column would leave the
    // strip's active window pointing at something hidden.
    if (m_columns.at(neighbourIdx).isFullyMinimized() || col->tiles.at(0).minimized) {
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
    reanchorAfterFocusChange(-1, oldViewOffset, params);
    return true;
}

QVector<int> ScrollStrip::visibleColumnIndices(const ScrollLayoutParams& params) const
{
    // Visible = the column's strip span intersects the viewport. Fully
    // minimized columns resolve to zero width and never qualify.
    const int viewOffset = viewOffsetFor(params);
    const int workW = params.workArea.width();
    QVector<int> visible;
    // Strip x accumulated in the walk rather than re-derived per index: the
    // accumulation is columnStripPos's own body (skip zero-width columns, else
    // advance by width + gap), and calling it per index re-resolved every
    // earlier column's width on every step.
    int stripX = 0;
    for (int i = 0; i < m_columns.size(); ++i) {
        const int w = columnExtentPx(m_columns.at(i), params);
        if (w <= 0) {
            continue;
        }
        const int x = stripX - viewOffset;
        if (x < workW && x + w > 0) {
            visible.append(i);
        }
        stripX += w + params.gap;
    }
    return visible;
}

int ScrollStrip::rotateVisibleColumns(bool clockwise, const ScrollLayoutParams& params)
{
    const QVector<int> visible = visibleColumnIndices(params);
    if (visible.size() < 2) {
        return 0;
    }
    // Rotate tiles + active-tile slot through the visible columns; width
    // and display stay with the SLOT so the strip's geometry holds still.
    QVector<QVector<Tile>> tiles;
    QVector<int> activeTileIdx;
    tiles.reserve(visible.size());
    activeTileIdx.reserve(visible.size());
    for (const int idx : visible) {
        tiles.append(m_columns.at(idx).tiles);
        activeTileIdx.append(m_columns.at(idx).activeTileIdx);
    }
    int rotated = 0;
    const int n = visible.size();
    const int widthBefore = stripExtentPx(params);
    for (int i = 0; i < n; ++i) {
        const int from = clockwise ? (i - 1 + n) % n : (i + 1) % n;
        Column& dest = m_columns[visible.at(i)];
        dest.tiles = tiles.at(from);
        dest.activeTileIdx = activeTileIdx.at(from);
        // Count non-minimized tiles only: the total feeds the user-facing
        // "Rotated %n windows" OSD copy, and a partly-minimized column's
        // hidden tiles did not visibly move. (visibleColumnIndices already
        // excludes FULLY minimized columns, so every counted column
        // contributes at least one — the caller's rotated < 2 no-op test
        // still holds.)
        for (const Tile& t : dest.tiles) {
            if (!t.minimized) {
                ++rotated;
            }
        }
    }
    // Width/display intents stayed with the slot, but a column's RESOLVED
    // width also carries its tiles' min-width clamp — so a rotate that lands
    // a wide-minimum window in a narrow slot really does change pixel widths,
    // and the total strip width with them. Re-clamp so the view cannot be
    // stranded past the end of a strip that just SHRANK — and only then:
    // centerActiveColumn deliberately stores out-of-range anchors
    // (restoreViewAnchor / updateViewForFocus document not clamping them),
    // so an unconditional clamp here would silently undo an explicit
    // centerColumn on a first/last column on the next rotate.
    if (params.workArea.isValid() && stripExtentPx(params) < widthBefore) {
        m_viewAnchor = clampedAnchor(m_viewAnchor, params);
    }
    return rotated;
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

bool ScrollStrip::toggleActiveWindowedFullscreen()
{
    // activeWindowId() already skips a minimized active tile, mirroring
    // relayout's fallback; toggling the raw activeTileIdx tile could flag
    // a window that never resolves and so never reaches the compositor —
    // while the engine verb's read-back (also activeWindowId-keyed) would
    // report a different window's state.
    const QString id = activeWindowId();
    if (id.isEmpty()) {
        return false;
    }
    return setWindowedFullscreen(id, !isWindowedFullscreen(id));
}

bool ScrollStrip::setWindowedFullscreen(const QString& windowId, bool on)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    Tile& tile = col.tiles[col.indexOfWindow(windowId)];
    if (tile.windowedFullscreen == on) {
        return false;
    }
    tile.windowedFullscreen = on;
    return true;
}

bool ScrollStrip::isWindowedFullscreen(const QString& windowId) const
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    const Column& col = m_columns.at(colIdx);
    return col.tiles.at(col.indexOfWindow(windowId)).windowedFullscreen;
}

} // namespace PhosphorScrollEngine
