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
 *      orthogonal centre distance. The result is deterministic for a given
 *      candidate list; two candidates identical on every tier (same overlap,
 *      gap within epsilon, equal perpendicular centre distance) resolve to the
 *      lower index.
 *
 * Coordinate-system agnostic: @p focus and every entry of @p candidates must
 * live in one shared space (absolute pixels, normalized [0,1], etc.).
 *
 * @param requireOverlap when true, candidates that do not overlap @p focus on
 *        the perpendicular axis (i.e. purely diagonal ones) are rejected
 *        outright rather than used as a last-resort fallback. Window
 *        navigation wants this: a diagonal tile is not a real left/right/up/down
 *        neighbour, so the move should hit the layout boundary (and cross to the
 *        next output) instead of swapping with a window that's offset away.
 *        Zone-adjacency / cross-output callers leave it false (default) so an
 *        offset-but-present neighbour still resolves.
 *
 * @return index into @p candidates, or -1 when no candidate lies in
 *         @p direction. Candidates sharing @p focus's centre are skipped.
 */
PHOSPHORGEOMETRY_EXPORT int directionalNeighbor(const QRectF& focus, const QList<QRectF>& candidates,
                                                Direction direction, bool requireOverlap = false);

/**
 * @brief The virtual desktop reached by stepping @p direction from
 *        @p currentDesktop on a @p rows-high desktop grid.
 *
 * Desktops are 1-based and laid out row-major across `ceil(desktopCount/rows)`
 * columns, matching KWin's DEFAULT (horizontal-fill) desktop-grid layout. KWin
 * can also be configured to fill the grid column-first; that orientation is not
 * expressible through @p rows alone and is not modelled here — under it the
 * Up/Down vs Left/Right mapping would differ. A step that leaves the grid —
 * past an edge, into a missing cell on a partial last row, or a horizontal move
 * that would wrap onto another row — returns 0 (no neighbour) rather than
 * wrapping, so callers can treat 0 as "no desktop that way; try another axis".
 *
 * @param rows grid height; values < 1 are clamped to 1 (a single-row grid).
 *
 * @return the 1-based target desktop, or 0 when there is no neighbour in
 *         @p direction (including for out-of-range inputs).
 */
PHOSPHORGEOMETRY_EXPORT int neighborDesktopInDirection(int currentDesktop, int desktopCount, int rows,
                                                       Direction direction);

} // namespace PhosphorGeometry
