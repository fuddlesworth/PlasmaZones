// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

namespace PhosphorScrollEngine {

int ScrollStrip::nearestPresetWidthIdx(const Column& c, const ScrollLayoutParams& params) const
{
    if (params.presetColumnWidths.isEmpty()) {
        return 0;
    }
    const int current = resolveColumnWidthPx(c.width, params);
    int best = 0;
    int bestDist = -1;
    for (int i = 0; i < params.presetColumnWidths.size(); ++i) {
        const int px = resolveColumnWidthPx(ColumnWidth::makePreset(i), params);
        const int dist = qAbs(px - current);
        if (bestDist < 0 || dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

int ScrollStrip::nearestPresetHeightIdx(const Tile& t, const ScrollLayoutParams& params) const
{
    if (params.presetWindowHeights.isEmpty()) {
        return 0;
    }
    const qreal current = currentHeightFraction(t, params);
    if (current < 0) {
        return 0; // Auto height: no determinate fraction — enter at the first preset
    }
    int best = 0;
    qreal bestDist = -1;
    for (int i = 0; i < params.presetWindowHeights.size(); ++i) {
        const qreal dist = qAbs(params.presetWindowHeights.at(i) - current);
        if (bestDist < 0 || dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    return best;
}

qreal ScrollStrip::currentHeightFraction(const Tile& t, const ScrollLayoutParams& params) const
{
    switch (t.height.kind) {
    case WindowHeight::Preset: {
        const int count = params.presetWindowHeights.size();
        return count > 0 ? params.presetWindowHeights.at(qBound(0, t.height.presetIdx, count - 1)) : -1;
    }
    case WindowHeight::Fixed:
        return params.workArea.height() > 0 ? static_cast<qreal>(t.height.fixedPx) / params.workArea.height() : -1;
    case WindowHeight::Auto:
        return -1;
    }
    return -1;
}

bool ScrollStrip::setActiveColumnWidth(const ColumnWidth& width)
{
    Column* col = activeColumnMutable();
    if (!col || col->width == width) {
        return false;
    }
    col->width = width;
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::cycleActiveColumnPresetWidth(int delta, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || params.presetColumnWidths.isEmpty() || (delta != -1 && delta != 1)) {
        return false;
    }
    const int count = params.presetColumnWidths.size();
    int idx;
    if (col->width.kind == ColumnWidth::Preset) {
        idx = (qBound(0, col->width.presetIdx, count - 1) + delta + count) % count;
    } else {
        // Enter the cycle from the nearest preset; step only if that preset
        // is already (near) the current size, so the first press lands on a
        // visible change.
        idx = nearestPresetWidthIdx(*col, params);
        const int nearPx = resolveColumnWidthPx(ColumnWidth::makePreset(idx), params);
        if (qAbs(nearPx - resolveColumnWidthPx(col->width, params)) <= 1) {
            idx = (idx + delta + count) % count;
        }
    }
    const ColumnWidth result = ColumnWidth::makePreset(idx);
    if (col->width == result) {
        // Single-entry preset list (or a step that landed where we already
        // are): report no change so the engine skips a pointless relayout.
        return false;
    }
    col->width = result;
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::adjustActiveColumnWidth(qreal deltaPercent, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    const int workW = params.workArea.width();
    if (workW <= 0) {
        return false; // degenerate area: qBound(1, …, workW) would invert
    }
    const int current = resolveColumnWidthPx(col->width, params);
    const int target = qBound(1, current + qRound(deltaPercent / 100.0 * workW), workW);
    if (target == current) {
        return false;
    }
    col->width = ColumnWidth::makeFixed(target);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::toggleMaximizeActiveColumn(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    const ColumnWidth full = ColumnWidth::makeProportion(1.0);
    if (m_preMaximizeColumnIdx == m_activeColumnIdx && col->width == full) {
        col->width = m_preMaximizeWidth;
        m_preMaximizeColumnIdx = -1;
        return true;
    }
    if (col->width == full) {
        // Full-width without a stored intent for THIS column (maximized in
        // an earlier session, or another column's maximize discarded the
        // single stored slot): fall back to the default width so the toggle
        // can always un-maximize instead of dead-ending.
        //
        // A user whose DEFAULT is itself full width would dead-end on that
        // fallback, so take half the work area in that case. The point of the
        // branch is that the toggle always does something.
        //
        // Compared in RESOLVED PIXELS, not on the intent value. ColumnWidth's
        // operator== compares kind first, so a default spelled Fixed(<work
        // area width>) is not == Proportion(1.0) even though it renders
        // identically: the old value compare took the "not full" arm, left
        // the column visually unchanged, and then reported TRUE — a false
        // success OSD on the exact dead-end this branch exists to remove.
        const bool defaultIsFullWidth =
            resolveColumnWidthPx(params.defaultColumnWidth, params) >= params.workArea.width();
        col->width = defaultIsFullWidth ? ColumnWidth::makeProportion(0.5) : params.defaultColumnWidth;
        // Unconditionally true: both arms leave the column narrower than the
        // full width it had on entry, so the toggle always did something.
        return true;
    }
    m_preMaximizeWidth = col->width;
    m_preMaximizeColumnIdx = m_activeColumnIdx;
    col->width = full;
    return true;
}

bool ScrollStrip::expandActiveColumnToAvailableWidth(const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    // On-screen leftover: the columns are contiguous in strip space, so the
    // occupied viewport region is one interval — everything outside it is
    // reclaimable.
    const int workW = params.workArea.width();
    const int viewX = viewXFor(params);
    const int stripW = stripWidthPx(params);
    const int covered = qMax(0, qMin(workW, stripW - viewX) - qMax(0, -viewX));
    const int leftover = workW - covered;
    if (leftover <= 0) {
        return false;
    }
    const int current = resolveColumnWidthPx(col->width, params);
    col->width = ColumnWidth::makeFixed(qMin(workW, current + leftover));
    if (m_preMaximizeColumnIdx == m_activeColumnIdx) {
        m_preMaximizeColumnIdx = -1;
    }
    return true;
}

bool ScrollStrip::setActiveWindowHeight(const WindowHeight& height)
{
    // Lone tiles included: relayout honors an explicit Fixed/Preset height
    // for a solo tile (niri parity), so the write is meaningful at any
    // stack size.
    Tile* tile = activeTileMutable();
    if (!tile || tile->height == height) {
        return false;
    }
    tile->height = height;
    return true;
}

bool ScrollStrip::cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile || params.presetWindowHeights.isEmpty() || (delta != -1 && delta != 1)) {
        return false;
    }
    const int count = params.presetWindowHeights.size();
    int idx;
    if (tile->height.kind == WindowHeight::Preset) {
        idx = (qBound(0, tile->height.presetIdx, count - 1) + delta + count) % count;
    } else {
        // Mirror the width cycle: enter from the nearest preset, stepping
        // once when that preset already matches the current height so the
        // first press always lands on a visible change.
        idx = nearestPresetHeightIdx(*tile, params);
        const qreal nearFrac = params.presetWindowHeights.at(qBound(0, idx, count - 1));
        const qreal curFrac = currentHeightFraction(*tile, params);
        if (curFrac >= 0 && qAbs(nearFrac - curFrac) < 0.01) {
            idx = (idx + delta + count) % count;
        }
    }
    const WindowHeight result = WindowHeight::makePreset(idx);
    if (tile->height == result) {
        return false;
    }
    tile->height = result;
    return true;
}

bool ScrollStrip::adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params)
{
    // Lone tiles included: see setActiveWindowHeight.
    Tile* tile = activeTileMutable();
    if (!tile || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    const int workH = params.workArea.height();
    if (workH <= 0) {
        return false; // degenerate area: qBound(1, …, workH) would invert
    }
    // Current pixel height: read it off a fresh relayout so the adjustment
    // starts from what is actually on screen. A targeted per-tile helper
    // cannot replace this: an Auto height only gets a pixel value from the
    // full column distribution (floors, budget rebalance), so the relayout
    // IS the resolution. Shortcut-rate path, not per-frame.
    int currentPx = workH;
    const ResolvedStrip resolved = relayout(params);
    for (const ResolvedColumn& rc : resolved.columns) {
        if (rc.columnIndex != m_activeColumnIdx) {
            continue;
        }
        for (const ResolvedTile& rt : rc.tiles) {
            if (rt.windowId == tile->windowId) {
                currentPx = rt.rect.height();
            }
        }
    }
    // Clamp to the tile's own floor as well (niri clamps against the client
    // min size in the verb): without it a shrink below minHeight would
    // "succeed" here while relayout re-clamps, so every further press
    // reports success with nothing moving on screen. With
    // respectMinimumSize off, relayout stops re-clamping too, so the floor
    // drops to 1 — keeping it would invert the failure (the verb refusing a
    // shrink relayout would happily apply).
    const int floorPx = params.respectMinimumSize ? qMax(1, tile->minHeight) : 1;
    const int target = qBound(floorPx, currentPx + qRound(deltaPercent / 100.0 * workH), workH);
    if (target == currentPx) {
        return false;
    }
    tile->height = WindowHeight::makeFixed(target);
    return true;
}

bool ScrollStrip::setWindowHeightIntent(const QString& windowId, const WindowHeight& height)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0) {
        return false;
    }
    Column& col = m_columns[colIdx];
    Tile& tile = col.tiles[col.indexOfWindow(windowId)];
    if (tile.height == height) {
        return false;
    }
    tile.height = height;
    return true;
}

bool ScrollStrip::resetActiveColumnHeights()
{
    Column* col = activeColumnMutable();
    if (!col) {
        return false;
    }
    bool changed = false;
    for (Tile& tile : col->tiles) {
        const WindowHeight even = WindowHeight::makeAuto();
        if (!(tile.height == even)) {
            tile.height = even;
            changed = true;
        }
    }
    return changed;
}

bool ScrollStrip::reconcileWindowSize(const QString& windowId, const QSize& ackedSize, bool widthChanged,
                                      bool heightChanged)
{
    const int colIdx = columnOfWindow(windowId);
    // isEmpty (not merely isValid): a 0x0 ack is "valid" to QSize but would
    // reconcile into a 1px column.
    if (colIdx < 0 || ackedSize.isEmpty()) {
        return false;
    }
    Column& col = m_columns[colIdx];
    bool changed = false;
    // Only take the width when the resize actually MOVED it (the engine
    // compares against the last applied rect): a purely vertical interactive
    // resize must not convert a Proportion/Preset intent into Fixed pixels —
    // that would stop the column reflowing on work-area, preset-list, and
    // DPI changes.
    if (widthChanged) {
        const ColumnWidth acked = ColumnWidth::makeFixed(ackedSize.width());
        if (!(col.width == acked)) {
            col.width = acked;
            changed = true;
            // Same invariant as every other width mutator in this file: a
            // width write invalidates a pending maximize-toggle restore for
            // this column. Unlike those mutators this one is keyed on the
            // RESIZED column, which need not be the active one.
            if (m_preMaximizeColumnIdx == colIdx) {
                m_preMaximizeColumnIdx = -1;
            }
        }
    }
    // Same guard as width: a purely horizontal resize must not convert the
    // tile's height intent into Fixed pixels. Lone tiles included — relayout
    // honors a solo tile's Fixed height (niri parity), so an interactive
    // vertical resize of a lone window sticks instead of snapping back.
    if (heightChanged) {
        Tile& tile = col.tiles[col.indexOfWindow(windowId)];
        const WindowHeight ackedH = WindowHeight::makeFixed(ackedSize.height());
        if (!(tile.height == ackedH)) {
            tile.height = ackedH;
            changed = true;
        }
    }
    return changed;
}

} // namespace PhosphorScrollEngine
