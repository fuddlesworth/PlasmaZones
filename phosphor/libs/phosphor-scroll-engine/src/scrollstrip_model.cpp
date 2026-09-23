// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// ScrollStrip::stripModel: the whole-strip description a strip MAP renders.
// Its own translation unit because scrollstrip_relayout.cpp, where the
// position walks it reuses live, already sits past the file-size ceiling.

#include <PhosphorScrollEngine/ScrollStrip.h>

#include <QHash>

namespace PhosphorScrollEngine {

ScrollStripModel ScrollStrip::stripModel(const ScrollLayoutParams& params) const
{
    ScrollStripModel model;
    model.axis = params.axis;
    model.viewportPx = params.axis.mainSize(params.workArea);
    model.viewOffsetPx = viewOffsetFor(params);
    model.stripExtentPx = stripExtentPx(params);
    model.activeColumn = m_activeColumnIdx;

    // ONE relayout for the cross extents. relayout() omits minimized tiles
    // and, on a degenerate work area, every tile, so a tile the resolve does
    // not name reads as 0 rather than as a stale rect.
    const ResolvedStrip resolved = relayout(params);
    QHash<int, const ResolvedColumn*> resolvedByIndex;
    resolvedByIndex.reserve(resolved.columns.size());
    for (const ResolvedColumn& column : resolved.columns) {
        resolvedByIndex.insert(column.columnIndex, &column);
    }

    // The same accumulation columnStripPos runs, done ONCE across the walk
    // rather than once per column: a zero-extent (fully minimized) column
    // takes no position and no gap, which is what keeps this walk in step
    // with relayout's own accumulator and with stripExtentPx.
    int mainPos = 0;
    model.columns.reserve(m_columns.size());
    for (int i = 0; i < m_columns.size(); ++i) {
        const Column& column = m_columns.at(i);
        ScrollStripModelColumn out;
        out.index = i;
        out.stripPosPx = mainPos;
        out.extentPx = columnExtentPx(column, params);
        out.display = column.display;
        out.activeTile = column.activeTileIdx;
        out.maximized = column.maximizedToEdges;
        if (out.extentPx > 0) {
            mainPos += out.extentPx + params.gap;
        }

        const ResolvedColumn* resolvedColumn = resolvedByIndex.value(i, nullptr);
        out.tiles.reserve(column.tiles.size());
        for (const Tile& tile : column.tiles) {
            ScrollStripModelTile outTile;
            outTile.windowId = tile.windowId;
            outTile.minimized = tile.minimized;
            if (resolvedColumn) {
                for (const ResolvedTile& resolvedTile : resolvedColumn->tiles) {
                    if (resolvedTile.windowId == tile.windowId) {
                        outTile.crossPx = params.axis.crossSize(resolvedTile.rect);
                        break;
                    }
                }
            }
            out.tiles.append(outTile);
        }
        model.columns.append(out);
    }
    return model;
}

} // namespace PhosphorScrollEngine
