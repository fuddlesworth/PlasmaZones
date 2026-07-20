// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorGeometry/DirectionalNeighbor.h"

#include <QLatin1StringView>

#include <algorithm>
#include <cmath>
#include <limits>

namespace PhosphorGeometry {

std::optional<Direction> directionFromString(QStringView token)
{
    if (token == QLatin1String("left")) {
        return Direction::Left;
    }
    if (token == QLatin1String("right")) {
        return Direction::Right;
    }
    if (token == QLatin1String("up")) {
        return Direction::Up;
    }
    if (token == QLatin1String("down")) {
        return Direction::Down;
    }
    return std::nullopt;
}

namespace {

/// Overlap length of the intervals [a0,a1] and [b0,b1]; 0 when disjoint or
/// merely touching.
qreal spanOverlap(qreal a0, qreal a1, qreal b0, qreal b1)
{
    return std::max(qreal(0), std::min(a1, b1) - std::max(a0, b0));
}

} // namespace

int directionalNeighbor(const QRectF& focus, const QList<QRectF>& candidates, Direction direction, bool requireOverlap)
{
    const bool horizontal = (direction == Direction::Left || direction == Direction::Right);
    const bool forward = (direction == Direction::Right || direction == Direction::Down);
    const QPointF focusCenter = focus.center();

    // Primary-axis tie tolerance, scaled to the focus size so the one
    // primitive serves absolute-pixel rects and normalized [0,1] rects alike:
    // gaps within this slack count as equal and fall through to the
    // perpendicular tie-break. A fixed absolute epsilon cannot span both
    // spaces — 0.5 is sub-pixel dust in pixels but half the screen in [0,1].
    const qreal eps = std::max({focus.width(), focus.height(), qreal(1e-9)}) * qreal(1e-4);

    int bestIndex = -1;
    bool bestOverlaps = false;
    qreal bestGap = std::numeric_limits<qreal>::max();
    qreal bestPerp = std::numeric_limits<qreal>::max();
    qreal bestAxis = std::numeric_limits<qreal>::max();

    for (int i = 0; i < candidates.size(); ++i) {
        const QRectF& c = candidates.at(i);
        const QPointF cc = c.center();
        if (cc == focusCenter) {
            continue;
        }

        qreal gap = 0;
        qreal perp = 0;
        qreal axis = 0;
        bool overlaps = false;

        if (horizontal) {
            // In-direction filter on the travel (x) axis.
            if (forward ? (cc.x() <= focusCenter.x()) : (cc.x() >= focusCenter.x())) {
                continue;
            }
            gap = forward ? (c.left() - focus.right()) : (focus.left() - c.right());
            overlaps = spanOverlap(focus.top(), focus.bottom(), c.top(), c.bottom()) > 0;
            perp = std::abs(cc.y() - focusCenter.y());
            axis = std::abs(cc.x() - focusCenter.x());
        } else {
            if (forward ? (cc.y() <= focusCenter.y()) : (cc.y() >= focusCenter.y())) {
                continue;
            }
            gap = forward ? (c.top() - focus.bottom()) : (focus.top() - c.bottom());
            overlaps = spanOverlap(focus.left(), focus.right(), c.left(), c.right()) > 0;
            perp = std::abs(cc.x() - focusCenter.x());
            axis = std::abs(cc.y() - focusCenter.y());
        }

        // Window navigation: a purely diagonal candidate (no perpendicular
        // overlap) is not a real neighbour in this direction — reject it so the
        // caller treats this as a layout boundary and crosses to the next
        // output, rather than swapping with a window that sits up/down (or
        // left/right) of the focus.
        if (requireOverlap && !overlaps) {
            continue;
        }

        // Rects overlapping on the primary axis too yield a negative gap; clamp
        // so they rank by the tie-breaks below rather than by sign. Candidates
        // in a stack of primary-axis-overlapping zones therefore all tie at
        // gap 0, which is why the travel-axis centre distance participates in
        // the ranking: without it the tie falls through to list order, and
        // list order is direction-asymmetric (zones are typically stored
        // left-to-right, so "left" would skip to the farthest zone).
        gap = std::max(qreal(0), gap);

        bool better = false;
        if (overlaps != bestOverlaps) {
            // An overlapping (side-by-side) candidate always beats a
            // non-overlapping (diagonal) one, however near the latter sits.
            better = overlaps;
        } else if (gap < bestGap - eps) {
            better = true;
        } else if (gap <= bestGap + eps) {
            if (perp < bestPerp - eps) {
                better = true;
            } else if (perp <= bestPerp + eps && axis < bestAxis - eps) {
                // Equal gap and equal perpendicular offset: prefer the
                // candidate whose centre is nearest along the travel axis, so
                // stacked overlapping zones step one neighbour at a time in
                // BOTH directions instead of resolving by index order.
                better = true;
            }
        }

        if (better) {
            bestIndex = i;
            bestOverlaps = overlaps;
            bestGap = gap;
            bestPerp = perp;
            bestAxis = axis;
        }
    }

    return bestIndex;
}

int edgeMostRect(const QList<QRectF>& candidates, Direction edge)
{
    // Rank by signed distance so every edge maximizes: negate the coordinates
    // for the min-seeking edges (Left / Up). Primary key: the edge-facing side
    // (the rect touching or reaching furthest toward @p edge). Secondary key,
    // for rects tying on the edge: the FAR side nearest the edge — the tile
    // confined to that edge beats one merely spanning to it (a full-height
    // master column and a bottom stack tile both touch the bottom edge; the
    // stack tile is the natural "up"-crossing entry). Remaining ties resolve
    // to the lowest index.
    const auto keys = [edge](const QRectF& r) -> std::pair<qreal, qreal> {
        switch (edge) {
        case Direction::Left:
            return {-r.left(), -r.right()};
        case Direction::Right:
            return {r.right(), r.left()};
        case Direction::Up:
            return {-r.top(), -r.bottom()};
        case Direction::Down:
            return {r.bottom(), r.top()};
        }
        return {0, 0};
    };

    int best = -1;
    std::pair<qreal, qreal> bestKeys{0, 0};
    for (int i = 0; i < candidates.size(); ++i) {
        const std::pair<qreal, qreal> k = keys(candidates.at(i));
        if (best < 0 || k > bestKeys) {
            best = i;
            bestKeys = k;
        }
    }
    return best;
}

int neighborDesktopInDirection(int currentDesktop, int desktopCount, int rows, Direction direction)
{
    if (desktopCount < 1 || currentDesktop < 1 || currentDesktop > desktopCount) {
        return 0;
    }
    const int r = std::max(1, rows);
    // Row-major grid; the last row may be partial.
    const int columns = (desktopCount + r - 1) / r;
    const int index = currentDesktop - 1;
    int row = index / columns;
    int col = index % columns;

    switch (direction) {
    case Direction::Left:
        col -= 1;
        break;
    case Direction::Right:
        col += 1;
        break;
    case Direction::Up:
        row -= 1;
        break;
    case Direction::Down:
        row += 1;
        break;
    }

    // Off the grid (edge, or a horizontal move that would wrap onto another
    // row) → no neighbour.
    if (row < 0 || row >= r || col < 0 || col >= columns) {
        return 0;
    }
    const int target = row * columns + col;
    // A missing cell on a partial last row is also "no neighbour".
    if (target >= desktopCount) {
        return 0;
    }
    return target + 1;
}

} // namespace PhosphorGeometry
