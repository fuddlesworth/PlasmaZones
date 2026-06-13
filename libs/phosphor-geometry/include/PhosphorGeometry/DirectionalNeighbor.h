// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "phosphorgeometry_export.h"

#include <QList>
#include <QRectF>
#include <QStringView>

#include <optional>

namespace PhosphorGeometry {

/// Cardinal navigation direction in screen space (y grows downward).
enum class Direction {
    Left,
    Right,
    Up,
    Down
};

/// Parse a lower-case direction token ("left"/"right"/"up"/"down") into a
/// Direction. Returns std::nullopt for any other token so callers reject
/// unknown input rather than silently defaulting to a cardinal direction.
PHOSPHORGEOMETRY_EXPORT std::optional<Direction> directionFromString(QStringView token);

/**
 * @brief Pick the spatial neighbour of @p focus among @p candidates in @p direction.
 *
 * Selection, in priority order:
 *   1. **In-direction filter** — a candidate qualifies only if it lies on the
 *      @p direction side of @p focus (its centre is past @p focus's centre on
 *      the travel axis).
 *   2. **Perpendicular-overlap preference** — qualifying candidates whose span
 *      on the axis orthogonal to travel overlaps @p focus's span (genuinely
 *      side-by-side) beat any candidate that does not overlap. This is what
 *      makes "right" from a top-left tile pick the top-right tile, never the
 *      bottom-right one, regardless of insertion order.
 *   3. **Nearest along the travel axis** — within the preferred tier, the
 *      smallest edge gap toward @p direction wins.
 *   4. **Perpendicular-centre tie-break** — equal gaps break by the smaller
 *      orthogonal centre distance, so the result is deterministic and never
 *      depends on candidate ordering.
 *
 * Coordinate-system agnostic: @p focus and every entry of @p candidates must
 * live in one shared space (absolute pixels, normalized [0,1], etc.).
 *
 * @return index into @p candidates, or -1 when no candidate lies in
 *         @p direction. Candidates sharing @p focus's centre are skipped.
 */
PHOSPHORGEOMETRY_EXPORT int directionalNeighbor(const QRectF& focus, const QList<QRectF>& candidates,
                                                Direction direction);

/**
 * @brief The virtual desktop reached by stepping @p direction from
 *        @p currentDesktop on a @p rows-high desktop grid.
 *
 * Desktops are 1-based and laid out row-major across `ceil(desktopCount/rows)`
 * columns, matching KWin's desktop-grid model. A step that leaves the grid —
 * past an edge, into a missing cell on a partial last row, or a horizontal move
 * that would wrap onto another row — returns 0 (no neighbour) rather than
 * wrapping, so callers can treat 0 as "no desktop that way; try another axis".
 *
 * @return the 1-based target desktop, or 0 when there is no neighbour in
 *         @p direction (including for out-of-range inputs).
 */
PHOSPHORGEOMETRY_EXPORT int neighborDesktopInDirection(int currentDesktop, int desktopCount, int rows,
                                                       Direction direction);

} // namespace PhosphorGeometry
