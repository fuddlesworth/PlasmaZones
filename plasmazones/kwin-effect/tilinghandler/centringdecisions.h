// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QPointF>
#include <QRectF>

/**
 * @file centringdecisions.h
 * @brief The autotile centring pass's pure decision: whether the window's
 *        latest commanded origin still belongs to the tile its centring target
 *        names. No KWin types, so the rows are unit tested; the caller reads
 *        the commanded rect off the live window.
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

} // namespace PlasmaZones::CentringDecisions
