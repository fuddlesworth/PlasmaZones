// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "overviewdropresolver.h"

namespace PlasmaZones {

namespace {

int mainPos(const QRect& r, bool vertical)
{
    return vertical ? r.y() : r.x();
}

int mainEnd(const QRect& r, bool vertical)
{
    return vertical ? r.y() + r.height() : r.x() + r.width();
}

int crossEnd(const QRect& r, bool vertical)
{
    return vertical ? r.x() + r.width() : r.y() + r.height();
}

int along(const QPoint& p, bool vertical)
{
    return vertical ? p.y() : p.x();
}

int across(const QPoint& p, bool vertical)
{
    return vertical ? p.x() : p.y();
}

} // namespace

bool stripIsVertical(const PhosphorEngine::OverviewStripEntry& strip)
{
    if (strip.columns.size() < 2) {
        return false;
    }
    const QRect first = strip.columns.first().rect;
    for (const PhosphorEngine::OverviewStripColumn& column : strip.columns) {
        if (column.rect.x() != first.x()) {
            return false;
        }
    }
    return true;
}

ScrollDropTarget resolveScrollDrop(const PhosphorEngine::OverviewStripEntry& strip, const QPoint& pos, bool vertical)
{
    ScrollDropTarget target;
    const int p = along(pos, vertical);
    for (int ci = 0; ci < strip.columns.size(); ++ci) {
        const PhosphorEngine::OverviewStripColumn& column = strip.columns.at(ci);
        const QRect rect = column.rect;
        // A column with no resolved rect (fully minimized) takes no drops
        // and splits no gap.
        if (rect.isNull()) {
            continue;
        }
        if (p < mainPos(rect, vertical)) {
            // Before this column: the gap between it and the previous one.
            target.column = ci;
            target.tile = -1;
            return target;
        }
        if (p < mainEnd(rect, vertical)) {
            // Inside the column: the tile under the point joins, a point
            // past every tile (below the stack) appends to the column.
            const int q = across(pos, vertical);
            target.column = ci;
            target.tile = column.tiles.size();
            for (int ti = 0; ti < column.tiles.size(); ++ti) {
                const QRect tile = column.tiles.at(ti).rect;
                if (tile.isNull()) {
                    continue;
                }
                // Above or inside this tile: the dragged window takes its
                // place and it moves down (niri InColumn(col, tile)).
                if (q < crossEnd(tile, vertical)) {
                    target.tile = ti;
                    break;
                }
            }
            return target;
        }
    }
    // Past the last column (or an empty strip): a new column at the end.
    target.column = strip.columns.size();
    target.tile = -1;
    return target;
}

} // namespace PlasmaZones
