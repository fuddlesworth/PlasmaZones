// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "geometryutils.h"
#include "zone.h"
#include "logging.h"
#include "layout.h"
#include "interfaces.h"
#include "constants.h"
#include "screenmanager.h"
#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QVariantMap>

namespace PlasmaZones {

namespace GeometryUtils {

static QRectF calculateZoneGeometry(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }

    const QRectF screenGeom = screen->geometry();
    return zone->calculateAbsoluteGeometry(screenGeom);
}

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, QScreen* screen)
{
    if (!screen) {
        return geometry;
    }

    // The overlay window covers the full screen (geometry()).
    // The geometry parameter is already in absolute screen coordinates
    // (from available or full-screen geometry), so we just need to convert
    // to overlay-local coordinates by subtracting the full screen origin.
    const QRectF screenGeom = screen->geometry();
    return QRectF(geometry.x() - screenGeom.x(), geometry.y() - screenGeom.y(), geometry.width(), geometry.height());
}

static QRectF calculateZoneGeometryInAvailableArea(Zone* zone, QScreen* screen)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Use actualAvailableGeometry which excludes panels/taskbars (queries PlasmaShell on Wayland)
    const QRectF availableGeom = ScreenManager::actualAvailableGeometry(screen);
    return zone->calculateAbsoluteGeometry(availableGeom);
}

/**
 * @brief Detect whether each edge of a zone lies at a screen boundary
 * @param zone The zone to check
 * @param screenGeom The reference screen geometry (for fixed mode pixel checks)
 * @return Array of 4 bools: [left, top, right, bottom] — true if at boundary
 */
struct EdgeBoundaries {
    bool left = false;
    bool top = false;
    bool right = false;
    bool bottom = false;
};

static EdgeBoundaries detectEdgeBoundaries(Zone* zone, const QRectF& screenGeom)
{
    EdgeBoundaries edges;

    if (zone->isFixedGeometry()) {
        // Fixed mode: pixel proximity check (within 5px of screen boundary)
        constexpr qreal pixelTolerance = 5.0;
        QRectF fixedGeo = zone->fixedGeometry();
        edges.left = (fixedGeo.left() < pixelTolerance);
        edges.top = (fixedGeo.top() < pixelTolerance);
        edges.right = (fixedGeo.right() > (screenGeom.width() - pixelTolerance));
        edges.bottom = (fixedGeo.bottom() > (screenGeom.height() - pixelTolerance));
    } else {
        // Relative mode: existing check (near 0 or 1, tolerance 0.01)
        constexpr qreal edgeTolerance = 0.01;
        QRectF relGeom = zone->relativeGeometry();
        edges.left = (relGeom.left() < edgeTolerance);
        edges.top = (relGeom.top() < edgeTolerance);
        edges.right = (relGeom.right() > (1.0 - edgeTolerance));
        edges.bottom = (relGeom.bottom() > (1.0 - edgeTolerance));
    }

    return edges;
}

QRectF getZoneGeometryWithGaps(Zone* zone, QScreen* screen, int innerGap, const EdgeGaps& outerGaps, bool useAvailableGeometry)
{
    if (!zone || !screen) {
        return QRectF();
    }

    // Use available geometry (excludes panels/taskbars) or full screen geometry
    QRectF geom;
    if (useAvailableGeometry) {
        geom = calculateZoneGeometryInAvailableArea(zone, screen);
    } else {
        geom = calculateZoneGeometry(zone, screen);
    }

    // Detect which edges are at screen boundaries
    QRectF screenGeom = useAvailableGeometry ? ScreenManager::actualAvailableGeometry(screen) : screen->geometry();
    EdgeBoundaries edges = detectEdgeBoundaries(zone, screenGeom);

    // Calculate adjustments for each edge
    qreal leftAdj = edges.left ? outerGaps.left : (innerGap / 2.0);
    qreal topAdj = edges.top ? outerGaps.top : (innerGap / 2.0);
    qreal rightAdj = edges.right ? outerGaps.right : (innerGap / 2.0);
    qreal bottomAdj = edges.bottom ? outerGaps.bottom : (innerGap / 2.0);

    // Apply the adjustments (positive inset from edges)
    geom = geom.adjusted(leftAdj, topAdj, -rightAdj, -bottomAdj);

    return geom;
}

int getEffectiveZonePadding(Layout* layout, ISettings* settings)
{
    // Check for layout-specific override first
    if (layout && layout->hasZonePaddingOverride()) {
        return layout->zonePadding();
    }
    // Fall back to global settings
    if (settings) {
        return settings->zonePadding();
    }
    // Last resort: use default constant
    return Defaults::ZonePadding;
}

QRect snapToRect(const QRectF& rf)
{
    // Round each edge independently, then derive width/height from the
    // rounded edges.  This guarantees that two adjacent zones whose QRectF
    // edges meet at the same fractional coordinate will round to the same
    // integer, preserving the exact configured gap between them.
    //
    // QRectF uses exclusive right/bottom: right = x + width.
    const int left = qRound(rf.x());
    const int top = qRound(rf.y());
    const int right = qRound(rf.x() + rf.width());
    const int bottom = qRound(rf.y() + rf.height());
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

EdgeGaps getEffectiveOuterGaps(Layout* layout, ISettings* settings)
{
    // Check for layout-specific per-side override first
    if (layout && layout->usePerSideOuterGap() && layout->hasPerSideOuterGapOverride()) {
        EdgeGaps gaps = layout->rawOuterGaps();
        // Fill in -1 sentinel values from global per-side or uniform fallback
        if (settings && settings->usePerSideOuterGap()) {
            if (gaps.top < 0) gaps.top = settings->outerGapTop();
            if (gaps.bottom < 0) gaps.bottom = settings->outerGapBottom();
            if (gaps.left < 0) gaps.left = settings->outerGapLeft();
            if (gaps.right < 0) gaps.right = settings->outerGapRight();
        } else {
            int fallback = settings ? settings->outerGap() : Defaults::OuterGap;
            if (gaps.top < 0) gaps.top = fallback;
            if (gaps.bottom < 0) gaps.bottom = fallback;
            if (gaps.left < 0) gaps.left = fallback;
            if (gaps.right < 0) gaps.right = fallback;
        }
        return gaps;
    }

    // Check for layout-specific uniform override
    if (layout && layout->hasOuterGapOverride()) {
        return EdgeGaps::uniform(layout->outerGap());
    }

    // Fall back to global settings
    if (settings) {
        if (settings->usePerSideOuterGap()) {
            return {settings->outerGapTop(), settings->outerGapBottom(),
                    settings->outerGapLeft(), settings->outerGapRight()};
        }
        return EdgeGaps::uniform(settings->outerGap());
    }

    return EdgeGaps::uniform(Defaults::OuterGap);
}

QRectF effectiveScreenGeometry(Layout* layout, QScreen* screen)
{
    if (!screen) {
        return QRectF();
    }
    if (layout && layout->useFullScreenGeometry()) {
        return screen->geometry();
    }
    return ScreenManager::actualAvailableGeometry(screen);
}

QRectF extractZoneGeometry(const QVariantMap& zone)
{
    return QRectF(zone.value(QLatin1String("x")).toDouble(), zone.value(QLatin1String("y")).toDouble(),
                  zone.value(QLatin1String("width")).toDouble(), zone.value(QLatin1String("height")).toDouble());
}

void setZoneGeometry(QVariantMap& zone, const QRectF& rect)
{
    zone[QLatin1String("x")] = rect.x();
    zone[QLatin1String("y")] = rect.y();
    zone[QLatin1String("width")] = rect.width();
    zone[QLatin1String("height")] = rect.height();
}

QString buildEmptyZonesJson(Layout* layout, QScreen* screen, ISettings* settings,
                            const std::function<bool(const Zone*)>& isZoneEmpty)
{
    if (!layout || !screen) {
        return QStringLiteral("[]");
    }

    bool useAvail = !(layout && layout->useFullScreenGeometry());
    layout->recalculateZoneGeometries(effectiveScreenGeometry(layout, screen));

    int zonePadding = getEffectiveZonePadding(layout, settings);
    EdgeGaps outerGaps = getEffectiveOuterGaps(layout, settings);

    QJsonArray arr;
    for (Zone* zone : layout->zones()) {
        if (!isZoneEmpty(zone)) {
            continue;
        }
        QRectF geom = getZoneGeometryWithGaps(zone, screen, zonePadding, outerGaps, useAvail);
        QRectF overlayGeom = availableAreaToOverlayCoordinates(geom, screen);

        QJsonObject obj;
        obj[JsonKeys::ZoneId] = zone->id().toString();
        obj[JsonKeys::X] = overlayGeom.x();
        obj[JsonKeys::Y] = overlayGeom.y();
        obj[JsonKeys::Width] = overlayGeom.width();
        obj[JsonKeys::Height] = overlayGeom.height();
        obj[JsonKeys::UseCustomColors] = zone->useCustomColors();
        obj[JsonKeys::HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
        obj[JsonKeys::InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
        obj[JsonKeys::BorderColor] = zone->borderColor().name(QColor::HexArgb);
        obj[JsonKeys::ActiveOpacity] = zone->activeOpacity();
        obj[JsonKeys::InactiveOpacity] = zone->inactiveOpacity();
        obj[JsonKeys::BorderWidth] = zone->useCustomColors()
            ? zone->borderWidth()
            : (settings ? settings->borderWidth() : Defaults::BorderWidth);
        obj[JsonKeys::BorderRadius] = zone->useCustomColors()
            ? zone->borderRadius()
            : (settings ? settings->borderRadius() : Defaults::BorderRadius);
        arr.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// ---------------------------------------------------------------------------
// Boundary-based constraint solver for enforceWindowMinSizes
// ---------------------------------------------------------------------------
//
// The algorithm works in two phases for each axis (horizontal then vertical):
//
// Phase 1 -- Column/Row group solver (handles regular grid layouts):
//   1. Identify column groups: zones sharing identical left AND right edges.
//   2. Collect unique vertical boundaries, sorted left-to-right.
//   3. For each column (span between consecutive boundaries), compute the
//      required minimum width as the max minWidth of any zone in that column.
//   4. Forward sweep: push boundaries right so each column meets its minimum.
//   5. Backward sweep: push boundaries left so each column meets its minimum.
//   6. If total minimums exceed available space, distribute proportionally.
//   7. Write adjusted boundaries back into zone geometries.
//   Repeat symmetrically for row groups on the vertical axis.
//
// Phase 2 -- Pairwise fallback (handles irregular BSP/Fibonacci grids):
//   For any zone still below its minimum, steal from any adjacent neighbor
//   that has surplus above *its* minimum. No hadDeficit guard -- any zone
//   with surplus can donate, which fixes the pass-through blocking bug.
// ---------------------------------------------------------------------------

/**
 * @brief Try the boundary-based constraint solver on one axis.
 * @return true if the zones formed a clean column/row grouping and were solved.
 *
 * "Clean grouping" means every zone's left/right (or top/bottom) edges align
 * with exactly one pair of consecutive boundaries. If any zone spans multiple
 * boundary intervals or straddles a boundary, we return false and let the
 * pairwise fallback handle it.
 */
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

    // Map each zone to its column index (span between boundaries[col] and boundaries[col+1]).
    // If a zone spans multiple columns, the layout is irregular -- bail out.
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

    // Compute minimum dimension for each column: max of minDim among zones in that column.
    // Unoccupied columns (inner gaps between zone groups) are treated as fixed-size
    // spacers: their minimum is locked to their current width so the forward/backward
    // sweep preserves the gap. Only occupied columns participate in redistribution.
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
        // Unsatisfiable -- first try to satisfy constrained columns and give
        // remainder to unconstrained ones. If even constrained columns' minimums
        // exceed the space, distribute proportionally among constrained columns.
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

/**
 * @brief Pairwise fallback: steal space from adjacent neighbors for zones below minimum.
 *
 * Unlike the old implementation, this does NOT use a hadDeficit guard: any zone
 * with surplus above its minimum can donate, even if it also needed (and received)
 * space earlier. This fixes the pass-through blocking bug where zone B received
 * space from C but then refused to donate to A.
 */
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
    // Moves the shared edge by delta pixels toward the donor.
    //
    // Uses single-edge matching: co-moves ALL zones sharing the specific
    // boundary being shifted, not just zones sharing both edges. This
    // correctly propagates shifts across BSP tree levels where zones at
    // different depths share a boundary but have different spans.
    // Example: in a BSP 5-window layout, Discord (full left half) shares
    // its right boundary with Steam (top-left quarter). When Steam steals
    // from Browser, Discord must also expand to keep the gap consistent.
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

                // Limit steal to prevent creating cross-zone overlaps:
                // check if expanding zone i would collide with any zone k
                // that won't be co-moved by applySteal.
                // Uses single-edge matching consistent with applySteal's
                // boundary-aware co-movement.
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
        // Extract each dimension independently: QSize(400, 0).isEmpty() is true
        // because height <= 0, but we still need the width constraint.
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
    // Min sizes are already incorporated by algorithms using their topology
    // knowledge. This is a lightweight safety net that catches residual
    // deficits from rounding or edge cases.
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

    // Final overlap cleanup, passing minSizes and innerGap so it respects constraints
    // and preserves gaps when resolving overlaps
    removeZoneOverlaps(zones, minSizes, innerGap);
}

// ---------------------------------------------------------------------------
// Min-size-aware overlap removal
// ---------------------------------------------------------------------------

void removeZoneOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes, int innerGap)
{
    if (zones.size() < 2) {
        return;
    }

    const int n = zones.size();
    const bool hasMinSizes = (minSizes.size() == n);

    constexpr int maxPasses = 5;
    for (int pass = 0; pass < maxPasses; ++pass) {
        bool changed = false;

        // Fix horizontal overlaps
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                // Check if they share vertical span AND overlap in x
                if (zones[i].bottom() < zones[j].top() || zones[j].bottom() < zones[i].top()) {
                    continue;
                }
                // Compute horizontal overlap using exclusive right edge
                int iRight = zones[i].left() + zones[i].width();
                int jRight = zones[j].left() + zones[j].width();
                int overlapLeft = qMax(zones[i].left(), zones[j].left());
                int overlapRight = qMin(iRight, jRight);
                if (overlapLeft >= overlapRight) {
                    continue;
                }

                // Determine which zone is on the left vs right
                int leftIdx = (zones[i].left() <= zones[j].left()) ? i : j;
                int rightIdx = (leftIdx == i) ? j : i;

                // Min-size-aware boundary placement:
                // Compute how much surplus each zone has above its minimum width.
                int leftMinW = AutotileDefaults::MinZoneSizePx;
                int rightMinW = AutotileDefaults::MinZoneSizePx;
                if (hasMinSizes) {
                    if (minSizes[leftIdx].width() > 0) {
                        leftMinW = qMax(leftMinW, minSizes[leftIdx].width());
                    }
                    if (minSizes[rightIdx].width() > 0) {
                        rightMinW = qMax(rightMinW, minSizes[rightIdx].width());
                    }
                }

                int leftSurplus = qMax(0, zones[leftIdx].width() - leftMinW);
                int rightSurplus = qMax(0, zones[rightIdx].width() - rightMinW);

                int boundary;
                int overlapAmount = overlapRight - overlapLeft;

                if (leftSurplus + rightSurplus <= 0) {
                    // Both at or below minimum -- split at midpoint as last resort
                    boundary = (overlapLeft + overlapRight) / 2;
                } else {
                    // Shift boundary toward the zone with more surplus
                    // (i.e., shrink the zone that can afford to lose space)
                    // boundary = overlapLeft + overlapAmount * rightSurplus / (leftSurplus + rightSurplus)
                    // means: if rightSurplus >> leftSurplus, boundary moves right, shrinking right zone
                    int leftShare = static_cast<int>(
                        static_cast<qint64>(overlapAmount) * rightSurplus / (leftSurplus + rightSurplus));
                    boundary = overlapLeft + leftShare;
                }

                // Clamp: don't shrink left zone below its minimum
                int leftExclusiveRight = zones[leftIdx].left() + leftMinW;
                boundary = qMax(boundary, leftExclusiveRight);
                // Clamp: don't shrink right zone below its minimum
                int rightMaxLeft = (zones[rightIdx].left() + zones[rightIdx].width()) - rightMinW;
                boundary = qMin(boundary, rightMaxLeft);

                // Apply: set right edge of left zone and left edge of right zone
                // Use exclusive-edge model: width = boundary - left
                // Offset boundary by innerGap so zones don't end up flush
                int leftBound = boundary;
                int rightBound = boundary;
                if (innerGap > 0) {
                    int halfGap = innerGap / 2;
                    int candidateLeft = boundary - halfGap;
                    int candidateRight = boundary + (innerGap - halfGap);
                    // Only apply gap if both zones stay above their minimums
                    if ((candidateLeft - zones[leftIdx].left()) >= leftMinW &&
                        ((zones[rightIdx].left() + zones[rightIdx].width()) - candidateRight) >= rightMinW) {
                        leftBound = candidateLeft;
                        rightBound = candidateRight;
                    }
                }

                int newLeftWidth = leftBound - zones[leftIdx].left();
                int newRightWidth = (zones[rightIdx].left() + zones[rightIdx].width()) - rightBound;

                if (newLeftWidth > 0 && newRightWidth > 0) {
                    zones[leftIdx].setWidth(newLeftWidth);
                    zones[rightIdx].setLeft(rightBound);
                    zones[rightIdx].setWidth(newRightWidth);
                    changed = true;
                }
            }
        }

        // Fix vertical overlaps
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                // Check if they share horizontal span AND overlap in y
                if (zones[i].right() < zones[j].left() || zones[j].right() < zones[i].left()) {
                    continue;
                }
                int iBottom = zones[i].top() + zones[i].height();
                int jBottom = zones[j].top() + zones[j].height();
                int overlapTop = qMax(zones[i].top(), zones[j].top());
                int overlapBottom = qMin(iBottom, jBottom);
                if (overlapTop >= overlapBottom) {
                    continue;
                }

                int topIdx = (zones[i].top() <= zones[j].top()) ? i : j;
                int bottomIdx = (topIdx == i) ? j : i;

                int topMinH = AutotileDefaults::MinZoneSizePx;
                int bottomMinH = AutotileDefaults::MinZoneSizePx;
                if (hasMinSizes) {
                    if (minSizes[topIdx].height() > 0) {
                        topMinH = qMax(topMinH, minSizes[topIdx].height());
                    }
                    if (minSizes[bottomIdx].height() > 0) {
                        bottomMinH = qMax(bottomMinH, minSizes[bottomIdx].height());
                    }
                }

                int topSurplus = qMax(0, zones[topIdx].height() - topMinH);
                int bottomSurplus = qMax(0, zones[bottomIdx].height() - bottomMinH);

                int boundary;
                int overlapAmount = overlapBottom - overlapTop;

                if (topSurplus + bottomSurplus <= 0) {
                    boundary = (overlapTop + overlapBottom) / 2;
                } else {
                    int topShare = static_cast<int>(
                        static_cast<qint64>(overlapAmount) * bottomSurplus / (topSurplus + bottomSurplus));
                    boundary = overlapTop + topShare;
                }

                int topExclusiveBottom = zones[topIdx].top() + topMinH;
                boundary = qMax(boundary, topExclusiveBottom);
                int bottomMaxTop = (zones[bottomIdx].top() + zones[bottomIdx].height()) - bottomMinH;
                boundary = qMin(boundary, bottomMaxTop);

                // Offset boundary by innerGap so zones don't end up flush
                int topBound = boundary;
                int bottomBound = boundary;
                if (innerGap > 0) {
                    int halfGap = innerGap / 2;
                    int candidateTop = boundary - halfGap;
                    int candidateBottom = boundary + (innerGap - halfGap);
                    if ((candidateTop - zones[topIdx].top()) >= topMinH &&
                        ((zones[bottomIdx].top() + zones[bottomIdx].height()) - candidateBottom) >= bottomMinH) {
                        topBound = candidateTop;
                        bottomBound = candidateBottom;
                    }
                }

                int newTopHeight = topBound - zones[topIdx].top();
                int newBottomHeight = (zones[bottomIdx].top() + zones[bottomIdx].height()) - bottomBound;

                if (newTopHeight > 0 && newBottomHeight > 0) {
                    zones[topIdx].setHeight(newTopHeight);
                    zones[bottomIdx].setTop(bottomBound);
                    zones[bottomIdx].setHeight(newBottomHeight);
                    changed = true;
                }
            }
        }

        if (!changed) {
            break;
        }
    }
}

} // namespace GeometryUtils

} // namespace PlasmaZones
