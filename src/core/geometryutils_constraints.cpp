// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Boundary-based constraint solver for enforceWindowMinSizes.
// Part of GeometryUtils namespace — split from geometryutils.cpp for SRP.

#include "geometryutils.h"
#include "constants.h"
#include "logging.h"
#include <algorithm>
#include <QRect>
#include <QSize>
#include <QVector>

namespace PlasmaZones {
namespace GeometryUtils {

// Try the boundary-based constraint solver on one axis.
// Returns true if zones form a clean column/row grouping and were solved.
static bool solveAxisBoundaries(QVector<QRect>& zones,
                                const QVector<int>& minDims, // minWidth or minHeight per zone
                                bool horizontal,             // true = width axis, false = height axis
                                int gapThreshold)            // max gap width to treat as adjacent
{
    const int n = zones.size();
    if (n == 0) {
        return true;
    }

    // Collect unique boundary positions (left/right or top/bottom edges).
    QVector<int> boundaries;
    for (int i = 0; i < n; ++i) {
        int lo = horizontal ? zones[i].left() : zones[i].top();
        // QRect::right() = left + width - 1, but we work with the exclusive
        // right edge (left + width) so boundaries tile without overlap.
        int hi = horizontal ? (zones[i].left() + zones[i].width()) : (zones[i].top() + zones[i].height());
        if (!boundaries.contains(lo)) {
            boundaries.append(lo);
        }
        if (!boundaries.contains(hi)) {
            boundaries.append(hi);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());

    const int numBoundaries = boundaries.size();
    if (numBoundaries < 2) {
        return true; // Degenerate: all zones at same position
    }

    const int numColumns = numBoundaries - 1;

    // Map each zone to its column. Bail out if zone spans multiple columns.
    QVector<int> zoneColumn(n, -1);
    QVector<bool> colOccupied(numColumns, false);
    for (int i = 0; i < n; ++i) {
        int lo = horizontal ? zones[i].left() : zones[i].top();
        int hi = horizontal ? (zones[i].left() + zones[i].width()) : (zones[i].top() + zones[i].height());
        int colStart = boundaries.indexOf(lo);
        int colEnd = boundaries.indexOf(hi);
        if (colStart < 0 || colEnd < 0 || colEnd != colStart + 1) {
            // Zone spans multiple columns or doesn't align -- irregular layout
            return false;
        }
        zoneColumn[i] = colStart;
        colOccupied[colStart] = true;
    }

    // Compute minimum dimension for each column. Unoccupied columns (gaps) are locked.
    QVector<int> colMinDim(numColumns, 0);
    QVector<bool> colHasConstraint(numColumns, false);
    for (int i = 0; i < n; ++i) {
        int col = zoneColumn[i];
        colMinDim[col] = qMax(colMinDim[col], minDims[i]);
        if (minDims[i] > 0) {
            colHasConstraint[col] = true;
        }
    }
    for (int c = 0; c < numColumns; ++c) {
        if (!colOccupied[c]) {
            int gapWidth = boundaries[c + 1] - boundaries[c];
            if (gapWidth > gapThreshold) {
                // Gap exceeds threshold: zones on either side are not adjacent.
                // Bail out and let pairwise fallback handle with its own adjacency checks.
                return false;
            }
            // Gap column: lock to current width so the sweep preserves it
            colMinDim[c] = gapWidth;
        } else if (colHasConstraint[c]) {
            colMinDim[c] = qMax(colMinDim[c], AutotileDefaults::MinZoneSizePx);
        } else {
            // Unconstrained occupied column: use 1px floor to prevent zero-width zones
            colMinDim[c] = qMax(colMinDim[c], 1);
        }
    }

    // Total available space
    const int totalSpace = boundaries.last() - boundaries.first();

    // Check if any column actually needs adjustment
    bool needsAdjustment = false;
    for (int c = 0; c < numColumns; ++c) {
        int currentDim = boundaries[c + 1] - boundaries[c];
        if (currentDim < colMinDim[c]) {
            needsAdjustment = true;
            break;
        }
    }
    if (!needsAdjustment) {
        return true;
    }

    // Check if total minimums exceed available space
    int totalMinDim = 0;
    for (int c = 0; c < numColumns; ++c) {
        totalMinDim += colMinDim[c];
    }

    QVector<int> newBoundaries = boundaries;

    if (totalMinDim > totalSpace) {
        // Unsatisfiable: satisfy constrained columns first, distribute remainder.
        int constrainedTotal = 0;
        int unconstrainedCount = 0;
        for (int c = 0; c < numColumns; ++c) {
            if (colHasConstraint[c]) {
                constrainedTotal += colMinDim[c];
            } else {
                ++unconstrainedCount;
            }
        }

        newBoundaries[0] = boundaries.first();
        if (constrainedTotal <= totalSpace) {
            // Constrained columns fit. Give them their minimums, split remainder
            // among unconstrained columns.
            int remainder = totalSpace - constrainedTotal;
            int unconstrainedAllocated = 0;
            for (int c = 0; c < numColumns; ++c) {
                int allocated;
                if (colHasConstraint[c]) {
                    allocated = colMinDim[c];
                } else {
                    // Distribute remaining space equally among unconstrained columns
                    int share = (unconstrainedCount > 0)
                                    ? qMax(1, remainder / unconstrainedCount)
                                    : qMax(1, remainder);
                    allocated = qMin(share, remainder - unconstrainedAllocated);
                    allocated = qMax(1, allocated);
                    unconstrainedAllocated += allocated;
                    --unconstrainedCount;
                    remainder -= allocated;
                    unconstrainedCount = qMax(unconstrainedCount, 0);
                }
                newBoundaries[c + 1] = newBoundaries[c] + allocated;
            }
        } else {
            // Even constrained columns don't fit: distribute proportionally
            int remaining = totalSpace;
            int remainingMin = totalMinDim;
            for (int c = 0; c < numColumns; ++c) {
                int allocated;
                if (remainingMin > 0) {
                    allocated = static_cast<int>(
                        static_cast<qint64>(remaining) * colMinDim[c] / remainingMin);
                } else {
                    allocated = remaining;
                }
                allocated = qMax(1, qMin(allocated, remaining));
                remainingMin -= colMinDim[c];
                remaining -= allocated;
                newBoundaries[c + 1] = newBoundaries[c] + allocated;
            }
        }
        // Fixup: last boundary must equal the original screen edge
        newBoundaries[numColumns] = boundaries.last();
    } else {
        // Forward sweep: push boundaries right to satisfy minimums
        for (int c = 0; c < numColumns; ++c) {
            int minPos = newBoundaries[c] + colMinDim[c];
            if (newBoundaries[c + 1] < minPos) {
                newBoundaries[c + 1] = minPos;
            }
        }

        // Clamp: the last boundary must not exceed the original screen edge
        if (newBoundaries[numColumns] > boundaries.last()) {
            newBoundaries[numColumns] = boundaries.last();
        }

        // Backward sweep: push boundaries left to satisfy minimums from the right side
        for (int c = numColumns - 1; c >= 0; --c) {
            int maxPos = newBoundaries[c + 1] - colMinDim[c];
            if (newBoundaries[c] > maxPos) {
                newBoundaries[c] = maxPos;
            }
        }

        // Clamp: the first boundary must not go below the original screen edge
        if (newBoundaries[0] < boundaries.first()) {
            newBoundaries[0] = boundaries.first();
        }
    }

    // Write adjusted boundaries back into zone geometries
    for (int i = 0; i < n; ++i) {
        int col = zoneColumn[i];
        int newLo = newBoundaries[col];
        int newHi = newBoundaries[col + 1];
        if (horizontal) {
            zones[i].setLeft(newLo);
            zones[i].setWidth(newHi - newLo);
        } else {
            zones[i].setTop(newLo);
            zones[i].setHeight(newHi - newLo);
        }
    }

    return true;
}

// Pairwise fallback: steal space from adjacent neighbors for zones below minimum.
// No hadDeficit guard — any zone with surplus can donate.
static void pairwiseFallback(QVector<QRect>& zones,
                             const QVector<int>& minDims,
                             int gapThreshold,
                             bool horizontal) // true = width axis, false = height axis
{
    const int n = zones.size();

    // Overlap checks for adjacency detection on the perpendicular axis
    auto perpendicularOverlap = [&](int a, int b) -> bool {
        if (horizontal) {
            // For width stealing, zones must share vertical span
            return zones[a].bottom() >= zones[b].top() && zones[b].bottom() >= zones[a].top();
        } else {
            // For height stealing, zones must share horizontal span
            return zones[a].right() >= zones[b].left() && zones[b].right() >= zones[a].left();
        }
    };

    auto getDim = [&](int i) -> int {
        return horizontal ? zones[i].width() : zones[i].height();
    };

    auto getMinDim = [&](int i) -> int {
        return qMax(minDims[i], AutotileDefaults::MinZoneSizePx);
    };

    // Apply a boundary shift between requester and donor.
    // Uses single-edge matching: co-moves ALL zones sharing the boundary.
    auto applySteal = [&](int reqIdx, int donIdx, int delta, bool donorOnHighSide) {
        if (horizontal) {
            const int reqLeft = zones[reqIdx].left();
            const int reqRight = zones[reqIdx].right();
            const int donLeft = zones[donIdx].left();
            const int donRight = zones[donIdx].right();
            for (int k = 0; k < n; ++k) {
                if (donorOnHighSide) {
                    // Donor is to the right: expand requester right, shrink donor left
                    if (zones[k].right() == reqRight) {
                        zones[k].setRight(zones[k].right() + delta);
                    }
                    if (zones[k].left() == donLeft) {
                        zones[k].setLeft(zones[k].left() + delta);
                    }
                } else {
                    // Donor is to the left: expand requester left, shrink donor right
                    if (zones[k].left() == reqLeft) {
                        zones[k].setLeft(zones[k].left() - delta);
                    }
                    if (zones[k].right() == donRight) {
                        zones[k].setRight(zones[k].right() - delta);
                    }
                }
            }
        } else {
            const int reqTop = zones[reqIdx].top();
            const int reqBottom = zones[reqIdx].bottom();
            const int donTop = zones[donIdx].top();
            const int donBottom = zones[donIdx].bottom();
            for (int k = 0; k < n; ++k) {
                if (donorOnHighSide) {
                    // Donor is below: expand requester bottom, shrink donor top
                    if (zones[k].bottom() == reqBottom) {
                        zones[k].setBottom(zones[k].bottom() + delta);
                    }
                    if (zones[k].top() == donTop) {
                        zones[k].setTop(zones[k].top() + delta);
                    }
                } else {
                    // Donor is above: expand requester top, shrink donor bottom
                    if (zones[k].top() == reqTop) {
                        zones[k].setTop(zones[k].top() - delta);
                    }
                    if (zones[k].bottom() == donBottom) {
                        zones[k].setBottom(zones[k].bottom() - delta);
                    }
                }
            }
        }
    };

    // Run multiple rounds until stable or we hit a safety limit
    constexpr int maxRounds = 10;
    for (int round = 0; round < maxRounds; ++round) {
        bool anyStolen = false;

        for (int i = 0; i < n; ++i) {
            int deficit = qMax(0, getMinDim(i) - getDim(i));
            if (deficit <= 0) {
                continue;
            }

            // Try to steal from any adjacent neighbor with surplus (no hadDeficit guard)
            for (int j = 0; j < n && deficit > 0; ++j) {
                if (i == j) {
                    continue;
                }
                if (!perpendicularOverlap(i, j)) {
                    continue;
                }

                int surplus = qMax(0, getDim(j) - getMinDim(j));
                if (surplus <= 0) {
                    continue;
                }

                bool donorOnHighSide = false;
                bool isAdjacent = false;

                if (horizontal) {
                    // Check if j is to the right of i
                    if (std::abs(zones[j].left() - (zones[i].left() + zones[i].width())) <= gapThreshold) {
                        donorOnHighSide = true;
                        isAdjacent = true;
                    }
                    // Check if j is to the left of i
                    else if (std::abs(zones[i].left() - (zones[j].left() + zones[j].width())) <= gapThreshold) {
                        donorOnHighSide = false;
                        isAdjacent = true;
                    }
                } else {
                    // Check if j is below i
                    if (std::abs(zones[j].top() - (zones[i].top() + zones[i].height())) <= gapThreshold) {
                        donorOnHighSide = true;
                        isAdjacent = true;
                    }
                    // Check if j is above i
                    else if (std::abs(zones[i].top() - (zones[j].top() + zones[j].height())) <= gapThreshold) {
                        donorOnHighSide = false;
                        isAdjacent = true;
                    }
                }

                if (!isAdjacent) {
                    continue;
                }

                int steal = qMin(deficit, surplus);

                // Limit steal to prevent cross-zone overlaps
                for (int k = 0; k < n && steal > 0; ++k) {
                    if (k == i || k == j) {
                        continue;
                    }
                    if (!perpendicularOverlap(i, k)) {
                        continue;
                    }
                    if (horizontal) {
                        // Would zone k be co-moved? (shares the moved boundary)
                        if (donorOnHighSide) {
                            if (zones[k].right() == zones[i].right()) continue; // co-expanded
                            if (zones[k].left() == zones[j].left()) continue;   // co-shrunk
                        } else {
                            if (zones[k].left() == zones[i].left()) continue;   // co-expanded
                            if (zones[k].right() == zones[j].right()) continue; // co-shrunk
                        }
                        if (donorOnHighSide) {
                            // Expanding right: clearance to zone k's left edge
                            int clearance = zones[k].left() - (zones[i].left() + zones[i].width());
                            if (clearance >= 0 && clearance < steal) {
                                steal = clearance;
                            }
                        } else {
                            // Expanding left: clearance to zone k's right edge
                            int clearance = zones[i].left() - (zones[k].left() + zones[k].width());
                            if (clearance >= 0 && clearance < steal) {
                                steal = clearance;
                            }
                        }
                    } else {
                        // Would zone k be co-moved? (shares the moved boundary)
                        if (donorOnHighSide) {
                            if (zones[k].bottom() == zones[i].bottom()) continue; // co-expanded
                            if (zones[k].top() == zones[j].top()) continue;       // co-shrunk
                        } else {
                            if (zones[k].top() == zones[i].top()) continue;       // co-expanded
                            if (zones[k].bottom() == zones[j].bottom()) continue; // co-shrunk
                        }
                        if (donorOnHighSide) {
                            int clearance = zones[k].top() - (zones[i].top() + zones[i].height());
                            if (clearance >= 0 && clearance < steal) {
                                steal = clearance;
                            }
                        } else {
                            int clearance = zones[i].top() - (zones[k].top() + zones[k].height());
                            if (clearance >= 0 && clearance < steal) {
                                steal = clearance;
                            }
                        }
                    }
                }

                if (steal > 0) {
                    applySteal(i, j, steal, donorOnHighSide);
                    deficit -= steal;
                    anyStolen = true;
                }
            }
        }

        if (!anyStolen) {
            break;
        }
    }
}

void enforceWindowMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes,
                           int gapThreshold, int innerGap)
{
    if (zones.size() != minSizes.size()) {
        return;
    }

    const int n = zones.size();
    if (n == 0) {
        return;
    }

    QVector<int> minWidths(n, 0);
    QVector<int> minHeights(n, 0);
    bool anyConstrained = false;
    for (int i = 0; i < n; ++i) {
        const QSize& ms = minSizes[i];
        if (ms.width() > 0) {
            minWidths[i] = ms.width();
        }
        if (ms.height() > 0) {
            minHeights[i] = ms.height();
        }
        if (minWidths[i] > 0 || minHeights[i] > 0) {
            anyConstrained = true;
        }
    }
    if (!anyConstrained) {
        return;
    }

    qCDebug(lcCore) << "enforceWindowMinSizes: adjusting zones for window minimum sizes";

    // Phase 1: Boundary solver on all zones at once (single-call approach).
    bool widthSolved = solveAxisBoundaries(zones, minWidths, /*horizontal=*/true, gapThreshold);
    if (!widthSolved) {
        pairwiseFallback(zones, minWidths, gapThreshold, /*horizontal=*/true);
    } else {
        bool widthDeficit = false;
        for (int i = 0; i < n; ++i) {
            if (minWidths[i] > 0 && zones[i].width() < qMax(minWidths[i], AutotileDefaults::MinZoneSizePx)) {
                widthDeficit = true;
                break;
            }
        }
        if (widthDeficit) {
            pairwiseFallback(zones, minWidths, gapThreshold, /*horizontal=*/true);
        }
    }

    bool heightSolved = solveAxisBoundaries(zones, minHeights, /*horizontal=*/false, gapThreshold);
    if (!heightSolved) {
        pairwiseFallback(zones, minHeights, gapThreshold, /*horizontal=*/false);
    } else {
        bool heightDeficit = false;
        for (int i = 0; i < n; ++i) {
            if (minHeights[i] > 0 && zones[i].height() < qMax(minHeights[i], AutotileDefaults::MinZoneSizePx)) {
                heightDeficit = true;
                break;
            }
        }
        if (heightDeficit) {
            pairwiseFallback(zones, minHeights, gapThreshold, /*horizontal=*/false);
        }
    }

    // Final overlap cleanup (respects constraints and preserves gaps)
    removeZoneOverlaps(zones, minSizes, innerGap);
}

} // namespace GeometryUtils
} // namespace PlasmaZones
