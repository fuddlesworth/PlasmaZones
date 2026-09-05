// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <PhosphorEngine/IOverviewModelSource.h>

#include <QPoint>

namespace PlasmaZones {

/// Where a window dropped on a scrolling workspace lands, mirroring niri's
/// `scrolling_insert_position`: inside a tile joins that column at that
/// tile's position, anywhere else along the strip becomes a new column at
/// the gap the point falls in (before the first column, between two, or
/// after the last). Pure geometry over the engine's strip entry so the
/// daemon test can drive it with hand-built strips.
struct ScrollDropTarget
{
    /// Column index: the column joined, or the index the new column takes.
    int column = 0;
    /// Tile index inside that column, or -1 for a new column.
    int tile = -1;

    bool operator==(const ScrollDropTarget& other) const = default;
};

/// Resolve @p pos (engine coordinate space, the same space the strip's
/// rects are in) against @p strip. @p vertical says the strip's main axis
/// runs down the screen (columns stack vertically); the builder derives it
/// from the column rects. An empty strip answers a new first column.
ScrollDropTarget resolveScrollDrop(const PhosphorEngine::OverviewStripEntry& strip, const QPoint& pos, bool vertical);

/// Whether the strip's columns stack vertically: every column shares the
/// same left edge while their tops differ. A single column is horizontal.
bool stripIsVertical(const PhosphorEngine::OverviewStripEntry& strip);

} // namespace PlasmaZones
