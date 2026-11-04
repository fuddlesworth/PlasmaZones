// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointF>
#include <QRectF>

/**
 * @file centringdecisions.h
 * @brief The pure decisions of framecentering.cpp: whether the window's
 *        latest commanded origin still belongs to the tile its centring target
 *        names, and whether a decoration change moves the minimum the engine
 *        models for the tile. No KWin types, so the rows are unit tested; the
 *        callers read the live window.
 */

namespace PlasmaZones::CentringDecisions {

/// Whether @p commandedOrigin (the top-left of the window's moveResizeGeometry)
/// is still inside the tile @p zone. A tile apply and every refusing or
/// oversized commit keep the origin at the zone origin, a centred commit moves
/// it right and down by less than half the zone, and a pixel of rounding either
/// way is tolerated. The far edges are excluded: a gapless neighbour zone
/// starts exactly there, so a script or KWin move into it reads as leaving this
/// tile (F536). QRectF::contains counts its edges, so the zone is shifted by one
/// pixel up and left rather than grown.
inline bool commandStillInTile(const QRectF& zone, const QPointF& commandedOrigin)
{
    return zone.adjusted(-1.0, -1.0, -1.0, -1.0).contains(commandedOrigin);
}

/// Whether a decoration change on a tiled window is one the engine's minimum
/// should follow. The minimum is reported as a FRAME size, the client's minimum
/// inside the decoration, so it only describes the tile while the decoration is
/// the one the tile wears. KWin's borderless maximized windows option takes the
/// decoration off for a full maximize and puts it back on the restore. A
/// maximize the user asked for (@p requestedMaximized without
/// @p maximizeOwnedByTiling) is not the tile's state when its decoration
/// changes. On a tiling screen the next batch clears it. On a scrolling screen
/// the interception hands it to the engine, and the batch that answers makes
/// it the column's maximize. Its poll then reports the minimum, or the
/// decoration change does when the title bar comes off after that batch.
/// Either way the minimum without the title bar is not one to report before
/// that batch. Reported, the changed minimum re-tiled the screen and that
/// batch cleared the maximize the user had just made, so the window jumped
/// straight back to its tile (discussion #1182). A maximize the tiling put on the window (a monocle
/// stack, a column maximized to the edges, or its own maximize write still in
/// progress) is the tile's state, and its decoration is the tile's.
inline constexpr bool decorationChangeMovesTileMinimum(bool requestedMaximized, bool maximizeOwnedByTiling)
{
    return !requestedMaximized || maximizeOwnedByTiling;
}

} // namespace PlasmaZones::CentringDecisions
