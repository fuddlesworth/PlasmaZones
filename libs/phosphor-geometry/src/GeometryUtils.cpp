// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorGeometry/GeometryUtils.h>
#include <PhosphorGeometry/JsonKeys.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace PhosphorGeometry {

QRectF availableAreaToOverlayCoordinates(const QRectF& geometry, const QRect& overlayGeometry)
{
    return QRectF(geometry.x() - overlayGeometry.x(), geometry.y() - overlayGeometry.y(), geometry.width(),
                  geometry.height());
}

QRect snapToRect(const QRectF& rf)
{
    const int left = qRound(rf.x());
    const int top = qRound(rf.y());
    const int right = qRound(rf.x() + rf.width());
    const int bottom = qRound(rf.y() + rf.height());
    return QRect(left, top, std::max(0, right - left), std::max(0, bottom - top));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Boundary-based constraint solver for enforceMinSizes
// ═══════════════════════════════════════════════════════════════════════════════

static bool solveAxisBoundaries(QVector<QRect>& zones, const QVector<int>& minDims, bool horizontal, int gapThreshold)
{
    const int n = zones.size();
    if (n == 0) {
        return true;
    }

    QVector<int> boundaries;
    for (int i = 0; i < n; ++i) {
        int lo = horizontal ? zones[i].left() : zones[i].top();
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
        return true;
    }

    const int numColumns = numBoundaries - 1;

    QVector<int> zoneColumn(n, -1);
    QVector<bool> colOccupied(numColumns, false);
    for (int i = 0; i < n; ++i) {
        int lo = horizontal ? zones[i].left() : zones[i].top();
        int hi = horizontal ? (zones[i].left() + zones[i].width()) : (zones[i].top() + zones[i].height());
        int colStart = boundaries.indexOf(lo);
        int colEnd = boundaries.indexOf(hi);
        if (colStart < 0 || colEnd < 0 || colEnd != colStart + 1) {
            return false;
        }
        zoneColumn[i] = colStart;
        colOccupied[colStart] = true;
    }

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
                return false;
            }
            colMinDim[c] = gapWidth;
        } else if (colHasConstraint[c]) {
            colMinDim[c] = qMax(colMinDim[c], GeometryDefaults::MinRectSizePx);
        } else {
            colMinDim[c] = qMax(colMinDim[c], 1);
        }
    }

    const int totalSpace = boundaries.last() - boundaries.first();

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

    int totalMinDim = 0;
    for (int c = 0; c < numColumns; ++c) {
        totalMinDim += colMinDim[c];
    }

    QVector<int> newBoundaries = boundaries;

    if (totalMinDim > totalSpace) {
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
            int remainder = totalSpace - constrainedTotal;
            int unconstrainedAllocated = 0;
            for (int c = 0; c < numColumns; ++c) {
                int allocated;
                if (colHasConstraint[c]) {
                    allocated = colMinDim[c];
                } else {
                    int share = (unconstrainedCount > 0) ? qMax(1, remainder / unconstrainedCount) : qMax(1, remainder);
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
            int remaining = totalSpace;
            int remainingMin = totalMinDim;
            for (int c = 0; c < numColumns; ++c) {
                int allocated;
                if (remainingMin > 0) {
                    allocated = static_cast<int>(static_cast<qint64>(remaining) * colMinDim[c] / remainingMin);
                } else {
                    allocated = remaining;
                }
                allocated = qMax(1, qMin(allocated, remaining));
                remainingMin -= colMinDim[c];
                remaining -= allocated;
                newBoundaries[c + 1] = newBoundaries[c] + allocated;
            }
        }
        newBoundaries[numColumns] = boundaries.last();
    } else {
        for (int c = 0; c < numColumns; ++c) {
            int minPos = newBoundaries[c] + colMinDim[c];
            if (newBoundaries[c + 1] < minPos) {
                newBoundaries[c + 1] = minPos;
            }
        }

        if (newBoundaries[numColumns] > boundaries.last()) {
            newBoundaries[numColumns] = boundaries.last();
        }

        for (int c = numColumns - 1; c >= 0; --c) {
            int maxPos = newBoundaries[c + 1] - colMinDim[c];
            if (newBoundaries[c] > maxPos) {
                newBoundaries[c] = maxPos;
            }
        }

        if (newBoundaries[0] < boundaries.first()) {
            newBoundaries[0] = boundaries.first();
        }
    }

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

static void pairwiseFallback(QVector<QRect>& zones, const QVector<int>& minDims, int gapThreshold, bool horizontal)
{
    const int n = zones.size();

    auto perpendicularOverlap = [&](int a, int b) -> bool {
        if (horizontal) {
            return zones[a].bottom() >= zones[b].top() && zones[b].bottom() >= zones[a].top();
        } else {
            return zones[a].right() >= zones[b].left() && zones[b].right() >= zones[a].left();
        }
    };

    auto getDim = [&](int i) -> int {
        return horizontal ? zones[i].width() : zones[i].height();
    };

    auto getMinDim = [&](int i) -> int {
        return qMax(minDims[i], GeometryDefaults::MinRectSizePx);
    };

    auto applySteal = [&](int reqIdx, int donIdx, int delta, bool donorOnHighSide) {
        if (horizontal) {
            const int reqLeft = zones[reqIdx].left();
            const int reqRight = zones[reqIdx].right();
            const int donLeft = zones[donIdx].left();
            const int donRight = zones[donIdx].right();
            for (int k = 0; k < n; ++k) {
                if (donorOnHighSide) {
                    if (zones[k].right() == reqRight) {
                        zones[k].setRight(zones[k].right() + delta);
                    }
                    if (zones[k].left() == donLeft) {
                        zones[k].setLeft(zones[k].left() + delta);
                    }
                } else {
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
                    if (zones[k].bottom() == reqBottom) {
                        zones[k].setBottom(zones[k].bottom() + delta);
                    }
                    if (zones[k].top() == donTop) {
                        zones[k].setTop(zones[k].top() + delta);
                    }
                } else {
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

    constexpr int maxRounds = 10;
    for (int round = 0; round < maxRounds; ++round) {
        bool anyStolen = false;

        for (int i = 0; i < n; ++i) {
            int deficit = qMax(0, getMinDim(i) - getDim(i));
            if (deficit <= 0) {
                continue;
            }

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
                    if (std::abs(zones[j].left() - (zones[i].left() + zones[i].width())) <= gapThreshold) {
                        donorOnHighSide = true;
                        isAdjacent = true;
                    } else if (std::abs(zones[i].left() - (zones[j].left() + zones[j].width())) <= gapThreshold) {
                        donorOnHighSide = false;
                        isAdjacent = true;
                    }
                } else {
                    if (std::abs(zones[j].top() - (zones[i].top() + zones[i].height())) <= gapThreshold) {
                        donorOnHighSide = true;
                        isAdjacent = true;
                    } else if (std::abs(zones[i].top() - (zones[j].top() + zones[j].height())) <= gapThreshold) {
                        donorOnHighSide = false;
                        isAdjacent = true;
                    }
                }

                if (!isAdjacent) {
                    continue;
                }

                int steal = qMin(deficit, surplus);

                for (int k = 0; k < n && steal > 0; ++k) {
                    if (k == i || k == j) {
                        continue;
                    }
                    if (!perpendicularOverlap(i, k)) {
                        continue;
                    }
                    if (horizontal) {
                        if (donorOnHighSide) {
                            if (zones[k].right() == zones[i].right())
                                continue;
                            if (zones[k].left() == zones[j].left())
                                continue;
                        } else {
                            if (zones[k].left() == zones[i].left())
                                continue;
                            if (zones[k].right() == zones[j].right())
                                continue;
                        }
                        if (donorOnHighSide) {
                            int clearance = zones[k].left() - (zones[i].left() + zones[i].width());
                            if (clearance >= 0 && clearance < steal) {
                                steal = clearance;
                            }
                        } else {
                            int clearance = zones[i].left() - (zones[k].left() + zones[k].width());
                            if (clearance >= 0 && clearance < steal) {
                                steal = clearance;
                            }
                        }
                    } else {
                        if (donorOnHighSide) {
                            if (zones[k].bottom() == zones[i].bottom())
                                continue;
                            if (zones[k].top() == zones[j].top())
                                continue;
                        } else {
                            if (zones[k].top() == zones[i].top())
                                continue;
                            if (zones[k].bottom() == zones[j].bottom())
                                continue;
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

void enforceMinSizes(QVector<QRect>& zones, const QVector<QSize>& minSizes, int gapThreshold, int innerGap)
{
    if (zones.isEmpty() || minSizes.isEmpty()) {
        return;
    }

    const int n = zones.size();

    QVector<int> minWidths(n, 0);
    QVector<int> minHeights(n, 0);
    bool anyConstrained = false;
    for (int i = 0; i < n; ++i) {
        const QSize ms = (i < minSizes.size()) ? minSizes[i] : QSize();
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

    bool widthSolved = solveAxisBoundaries(zones, minWidths, true, gapThreshold);
    if (!widthSolved) {
        pairwiseFallback(zones, minWidths, gapThreshold, true);
    } else {
        bool widthDeficit = false;
        for (int i = 0; i < n; ++i) {
            if (minWidths[i] > 0 && zones[i].width() < qMax(minWidths[i], GeometryDefaults::MinRectSizePx)) {
                widthDeficit = true;
                break;
            }
        }
        if (widthDeficit) {
            pairwiseFallback(zones, minWidths, gapThreshold, true);
        }
    }

    bool heightSolved = solveAxisBoundaries(zones, minHeights, false, gapThreshold);
    if (!heightSolved) {
        pairwiseFallback(zones, minHeights, gapThreshold, false);
    } else {
        bool heightDeficit = false;
        for (int i = 0; i < n; ++i) {
            if (minHeights[i] > 0 && zones[i].height() < qMax(minHeights[i], GeometryDefaults::MinRectSizePx)) {
                heightDeficit = true;
                break;
            }
        }
        if (heightDeficit) {
            pairwiseFallback(zones, minHeights, gapThreshold, false);
        }
    }

    removeRectOverlaps(zones, minSizes, innerGap);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Min-size-aware overlap removal (multi-pass convergence)
// ═══════════════════════════════════════════════════════════════════════════════

void removeRectOverlaps(QVector<QRect>& zones, const QVector<QSize>& minSizes, int innerGap)
{
    if (zones.size() < 2) {
        return;
    }

    const int n = zones.size();
    const bool hasMinSizes = (minSizes.size() == n);

    constexpr int maxPasses = 5;
    for (int pass = 0; pass < maxPasses; ++pass) {
        bool changed = false;

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (zones[i].bottom() < zones[j].top() || zones[j].bottom() < zones[i].top()) {
                    continue;
                }
                int iRight = zones[i].left() + zones[i].width();
                int jRight = zones[j].left() + zones[j].width();
                int overlapLeft = qMax(zones[i].left(), zones[j].left());
                int overlapRight = qMin(iRight, jRight);
                if (overlapLeft >= overlapRight) {
                    continue;
                }

                int leftIdx = (zones[i].left() <= zones[j].left()) ? i : j;
                int rightIdx = (leftIdx == i) ? j : i;

                int leftMinW = GeometryDefaults::MinRectSizePx;
                int rightMinW = GeometryDefaults::MinRectSizePx;
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
                    boundary = (overlapLeft + overlapRight) / 2;
                } else {
                    int leftShare = static_cast<int>(static_cast<qint64>(overlapAmount) * rightSurplus
                                                     / (leftSurplus + rightSurplus));
                    boundary = overlapLeft + leftShare;
                }

                int leftExclusiveRight = zones[leftIdx].left() + leftMinW;
                boundary = qMax(boundary, leftExclusiveRight);
                int rightMaxLeft = (zones[rightIdx].left() + zones[rightIdx].width()) - rightMinW;
                boundary = qMin(boundary, rightMaxLeft);

                int leftBound = boundary;
                int rightBound = boundary;
                if (innerGap > 0) {
                    int halfGap = innerGap / 2;
                    int candidateLeft = boundary - halfGap;
                    int candidateRight = boundary + (innerGap - halfGap);
                    if ((candidateLeft - zones[leftIdx].left()) >= leftMinW
                        && ((zones[rightIdx].left() + zones[rightIdx].width()) - candidateRight) >= rightMinW) {
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

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
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

                int topMinH = GeometryDefaults::MinRectSizePx;
                int bottomMinH = GeometryDefaults::MinRectSizePx;
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
                    int topShare = static_cast<int>(static_cast<qint64>(overlapAmount) * bottomSurplus
                                                    / (topSurplus + bottomSurplus));
                    boundary = overlapTop + topShare;
                }

                int topExclusiveBottom = zones[topIdx].top() + topMinH;
                boundary = qMax(boundary, topExclusiveBottom);
                int bottomMaxTop = (zones[bottomIdx].top() + zones[bottomIdx].height()) - bottomMinH;
                boundary = qMin(boundary, bottomMaxTop);

                int topBound = boundary;
                int bottomBound = boundary;
                if (innerGap > 0) {
                    int halfGap = innerGap / 2;
                    int candidateTop = boundary - halfGap;
                    int candidateBottom = boundary + (innerGap - halfGap);
                    if ((candidateTop - zones[topIdx].top()) >= topMinH
                        && ((zones[bottomIdx].top() + zones[bottomIdx].height()) - candidateBottom) >= bottomMinH) {
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

// ═══════════════════════════════════════════════════════════════════════════════
// Position-only bounds clamp
// ═══════════════════════════════════════════════════════════════════════════════

void clampZonesToScreen(QVector<QRect>& zones, const QVector<QSize>& minSizes, const QRect& screen)
{
    if (!screen.isValid() || zones.isEmpty()) {
        return;
    }
    const int screenLeft = screen.x();
    const int screenTop = screen.y();
    const int screenRight = screen.x() + screen.width();
    const int screenBottom = screen.y() + screen.height();

    for (int i = 0; i < zones.size(); ++i) {
        QRect& zone = zones[i];
        const QSize ms = (i < minSizes.size()) ? minSizes[i] : QSize(0, 0);
        const int effW = std::max(zone.width(), std::max(0, ms.width()));
        const int effH = std::max(zone.height(), std::max(0, ms.height()));

        if (zone.x() + effW > screenRight) {
            zone.moveLeft(std::max(screenLeft, screenRight - effW));
        }
        if (zone.y() + effH > screenBottom) {
            zone.moveTop(std::max(screenTop, screenBottom - effH));
        }
        if (zone.x() < screenLeft) {
            zone.moveLeft(screenLeft);
        }
        if (zone.y() < screenTop) {
            zone.moveTop(screenTop);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Utilities
// ═══════════════════════════════════════════════════════════════════════════════

QString rectToJson(const QRect& rect)
{
    QJsonObject obj;
    obj[JsonKeys::X] = rect.x();
    obj[JsonKeys::Y] = rect.y();
    obj[JsonKeys::Width] = rect.width();
    obj[JsonKeys::Height] = rect.height();
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

} // namespace PhosphorGeometry
