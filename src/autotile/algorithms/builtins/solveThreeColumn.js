// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Solve three-column layout widths with min-size constraints.
 * Uses injected PZ_MIN_ZONE_SIZE, PZ_MIN_SPLIT, PZ_MAX_SPLIT constants.
 * Returns {leftWidth, centerWidth, rightWidth, leftX, centerX, rightX}.
 *
 * Port of TilingAlgorithm::solveThreeColumnWidths.
 */
function solveThreeColumn(areaX, contentWidth, innerGap, splitRatio, minL, minC, minR) {
    if (contentWidth <= 0) {
        return {leftWidth:1, centerWidth:1, rightWidth:1, leftX:areaX, centerX:areaX, rightX:areaX};
    }
    var effMinLeft = Math.max(PZ_MIN_ZONE_SIZE, minL);
    var effMinRight = Math.max(PZ_MIN_ZONE_SIZE, minR);
    var maxCenter = Math.min(PZ_MAX_SPLIT,
        1.0 - (effMinLeft + effMinRight) / contentWidth);
    var centerRatio = Math.min(Math.max(splitRatio, PZ_MIN_SPLIT), Math.max(PZ_MIN_SPLIT, maxCenter));
    if (minC > 0) {
        var minCenterRatio = minC / contentWidth;
        centerRatio = Math.max(centerRatio, Math.min(minCenterRatio, maxCenter));
    }
    var sideRatio = (1.0 - centerRatio) / 2.0;
    var leftWidth = Math.floor(contentWidth * sideRatio);
    var centerWidth = Math.floor(contentWidth * centerRatio);
    var rightWidth = contentWidth - leftWidth - centerWidth;
    var totalColumnMin = Math.max(minL, 0) + Math.max(minC, 0) + Math.max(minR, 0);
    if (totalColumnMin > contentWidth && totalColumnMin > 0) {
        var eL = Math.max(minL, 1); var eC = Math.max(minC, 1); var eR = Math.max(minR, 1);
        var eT = eL + eC + eR;
        leftWidth = Math.floor(contentWidth * eL / eT);
        centerWidth = Math.floor(contentWidth * eC / eT);
        rightWidth = contentWidth - leftWidth - centerWidth;
    } else {
        if (minL > 0 && leftWidth < minL) {
            var deficit = minL - leftWidth; leftWidth = minL;
            var fromCenter = Math.max(0, Math.min(deficit, centerWidth - Math.max(minC, 1)));
            centerWidth -= fromCenter; rightWidth = contentWidth - leftWidth - centerWidth;
        }
        if (minR > 0 && rightWidth < minR) {
            var deficit = minR - rightWidth; rightWidth = minR;
            var fromCenter = Math.max(0, Math.min(deficit, centerWidth - Math.max(minC, 1)));
            centerWidth -= fromCenter; leftWidth = contentWidth - rightWidth - centerWidth;
        }
        if (minC > 0 && centerWidth < minC) {
            var deficit = minC - centerWidth; centerWidth = minC;
            if (leftWidth >= rightWidth) {
                var take = Math.min(deficit, leftWidth - 1);
                leftWidth -= take; deficit -= take;
                if (deficit > 0) rightWidth -= deficit;
            } else {
                var take = Math.min(deficit, rightWidth - 1);
                rightWidth -= take; deficit -= take;
                if (deficit > 0) leftWidth -= deficit;
            }
        }
    }
    leftWidth = Math.max(1, leftWidth);
    centerWidth = Math.max(1, centerWidth);
    rightWidth = Math.max(1, rightWidth);
    var colSum = leftWidth + centerWidth + rightWidth;
    if (colSum > contentWidth) {
        var excess = colSum - contentWidth;
        if (centerWidth >= leftWidth && centerWidth >= rightWidth) {
            centerWidth = Math.max(1, centerWidth - excess);
        } else if (leftWidth >= rightWidth) {
            leftWidth = Math.max(1, leftWidth - excess);
        } else {
            rightWidth = Math.max(1, rightWidth - excess);
        }
    }
    return {leftWidth: leftWidth, centerWidth: centerWidth, rightWidth: rightWidth,
        leftX: areaX, centerX: areaX + leftWidth + innerGap,
        rightX: areaX + leftWidth + innerGap + centerWidth + innerGap};
}
