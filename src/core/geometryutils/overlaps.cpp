// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Min-size-aware overlap removal.
// Part of GeometryUtils namespace — split from geometryutils.cpp for SRP.

#include "../geometryutils.h"
#include "autotile/AutotileConstants.h"
#include "../constants.h"
#include <QRect>
#include <QSize>
#include <QVector>

namespace PlasmaZones {
namespace GeometryUtils {

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

                // Min-size-aware boundary placement
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
                    int leftShare = static_cast<int>(static_cast<qint64>(overlapAmount) * rightSurplus
                                                     / (leftSurplus + rightSurplus));
                    boundary = overlapLeft + leftShare;
                }

                // Clamp: don't shrink left zone below its minimum
                int leftExclusiveRight = zones[leftIdx].left() + leftMinW;
                boundary = qMax(boundary, leftExclusiveRight);
                // Clamp: don't shrink right zone below its minimum
                int rightMaxLeft = (zones[rightIdx].left() + zones[rightIdx].width()) - rightMinW;
                boundary = qMin(boundary, rightMaxLeft);

                // Apply: set right edge of left zone and left edge of right zone
                // Offset boundary by innerGap so zones don't end up flush
                int leftBound = boundary;
                int rightBound = boundary;
                if (innerGap > 0) {
                    int halfGap = innerGap / 2;
                    int candidateLeft = boundary - halfGap;
                    int candidateRight = boundary + (innerGap - halfGap);
                    // Only apply gap if both zones stay above their minimums
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
                    int topShare = static_cast<int>(static_cast<qint64>(overlapAmount) * bottomSurplus
                                                    / (topSurplus + bottomSurplus));
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

} // namespace GeometryUtils
} // namespace PlasmaZones
