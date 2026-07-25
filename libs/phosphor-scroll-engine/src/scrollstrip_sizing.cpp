// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QtGlobal>

#include <cmath>

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

bool ScrollStrip::setActiveColumnWidth(const ColumnWidth& width)
{
    Column* col = activeColumnMutable();
    if (!col || col->width == width) {
        return false;
    }
    col->width = width;
    m_preMaximizeColumnIdx = -1;
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
        idx = (col->width.presetIdx + delta % count + count) % count;
    } else {
        // Enter the cycle from the nearest preset; step only if that preset
        // is already (near) the current size, so the first press lands on a
        // visible change.
        idx = nearestPresetWidthIdx(*col, params);
        const int nearPx = resolveColumnWidthPx(ColumnWidth::makePreset(idx), params);
        if (qAbs(nearPx - resolveColumnWidthPx(col->width, params)) <= 1) {
            idx = (idx + delta % count + count) % count;
        }
    }
    col->width = ColumnWidth::makePreset(idx);
    m_preMaximizeColumnIdx = -1;
    return true;
}

bool ScrollStrip::adjustActiveColumnWidth(qreal deltaPercent, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    if (!col || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    const int workW = params.workArea.width();
    const int current = resolveColumnWidthPx(col->width, params);
    const int target = qBound(1, current + qRound(deltaPercent / 100.0 * workW), workW);
    if (target == current) {
        return false;
    }
    col->width = ColumnWidth::makeFixed(target);
    m_preMaximizeColumnIdx = -1;
    return true;
}

bool ScrollStrip::toggleMaximizeActiveColumn()
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
        return false;
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
    m_preMaximizeColumnIdx = -1;
    return true;
}

bool ScrollStrip::setActiveWindowHeight(const WindowHeight& height)
{
    Tile* tile = activeTileMutable();
    if (!tile || tile->height == height) {
        return false;
    }
    tile->height = height;
    return true;
}

bool ScrollStrip::cycleActiveWindowPresetHeight(int delta, const ScrollLayoutParams& params)
{
    Tile* tile = activeTileMutable();
    if (!tile || params.presetWindowHeights.isEmpty() || (delta != -1 && delta != 1)) {
        return false;
    }
    const int count = params.presetWindowHeights.size();
    int idx = 0;
    if (tile->height.kind == WindowHeight::Preset) {
        idx = (tile->height.presetIdx + delta % count + count) % count;
    }
    tile->height = WindowHeight::makePreset(idx);
    return true;
}

bool ScrollStrip::adjustActiveWindowHeight(qreal deltaPercent, const ScrollLayoutParams& params)
{
    Column* col = activeColumnMutable();
    Tile* tile = activeTileMutable();
    if (!col || !tile || qFuzzyIsNull(deltaPercent)) {
        return false;
    }
    const int workH = params.workArea.height();
    // Current pixel height: read it off a fresh relayout so the adjustment
    // starts from what is actually on screen (Auto heights included).
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
    const int target = qBound(1, currentPx + qRound(deltaPercent / 100.0 * workH), workH);
    if (target == currentPx) {
        return false;
    }
    tile->height = WindowHeight::makeFixed(target);
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

bool ScrollStrip::reconcileWindowSize(const QString& windowId, const QSize& ackedSize)
{
    const int colIdx = columnOfWindow(windowId);
    if (colIdx < 0 || !ackedSize.isValid()) {
        return false;
    }
    Column& col = m_columns[colIdx];
    bool changed = false;
    const ColumnWidth acked = ColumnWidth::makeFixed(ackedSize.width());
    if (!(col.width == acked)) {
        col.width = acked;
        changed = true;
    }
    // Only meaningful for multi-tile columns: a lone tile always fills the
    // column height, so recording it as Fixed would fight the work area.
    if (col.tiles.size() > 1) {
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
