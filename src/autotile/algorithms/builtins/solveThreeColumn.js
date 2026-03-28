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
    const effMinLeft = Math.max(PZ_MIN_ZONE_SIZE, minL);
    const effMinRight = Math.max(PZ_MIN_ZONE_SIZE, minR);
    const maxCenter = Math.min(PZ_MAX_SPLIT,
        1.0 - (effMinLeft + effMinRight) / contentWidth);
    let centerRatio = Math.min(Math.max(splitRatio, PZ_MIN_SPLIT), Math.max(PZ_MIN_SPLIT, maxCenter));
    if (minC > 0) {
        const minCenterRatio = minC / contentWidth;
        centerRatio = Math.max(centerRatio, Math.min(minCenterRatio, maxCenter));
    }
    const sideRatio = (1.0 - centerRatio) / 2.0;
    let leftWidth = Math.floor(contentWidth * sideRatio);
    let centerWidth = Math.floor(contentWidth * centerRatio);
    let rightWidth = contentWidth - leftWidth - centerWidth;
    const totalColumnMin = Math.max(minL, 0) + Math.max(minC, 0) + Math.max(minR, 0);
    if (totalColumnMin > contentWidth && totalColumnMin > 0) {
        const eL = Math.max(minL, 1);
        const eC = Math.max(minC, 1);
        const eR = Math.max(minR, 1);
        const eT = eL + eC + eR;
        leftWidth = Math.floor(contentWidth * eL / eT);
        centerWidth = Math.floor(contentWidth * eC / eT);
        rightWidth = contentWidth - leftWidth - centerWidth;
    } else {
        if (minL > 0 && leftWidth < minL) {
            const deficit = minL - leftWidth;
            leftWidth = minL;
            const fromCenter = Math.max(0, Math.min(deficit, centerWidth - Math.max(minC, 1)));
            centerWidth -= fromCenter;
            rightWidth = contentWidth - leftWidth - centerWidth;
        }
        if (minR > 0 && rightWidth < minR) {
            const deficit = minR - rightWidth;
            rightWidth = minR;
            const fromCenter = Math.max(0, Math.min(deficit, centerWidth - Math.max(minC, 1)));
            centerWidth -= fromCenter;
            leftWidth = contentWidth - rightWidth - centerWidth;
        }
        if (minC > 0 && centerWidth < minC) {
            let deficit = minC - centerWidth;
            centerWidth = minC;
            if (leftWidth >= rightWidth) {
                const take = Math.min(deficit, leftWidth - 1);
                leftWidth -= take; deficit -= take;
                if (deficit > 0) rightWidth -= deficit;
            } else {
                const take = Math.min(deficit, rightWidth - 1);
                rightWidth -= take; deficit -= take;
                if (deficit > 0) leftWidth -= deficit;
            }
        }
    }
    leftWidth = Math.max(1, leftWidth);
    centerWidth = Math.max(1, centerWidth);
    rightWidth = Math.max(1, rightWidth);
    let colSum = leftWidth + centerWidth + rightWidth;
    while (colSum > contentWidth) {
        const excess = colSum - contentWidth;
        if (excess <= 0) break;
        // Take from largest column first
        if (centerWidth >= leftWidth && centerWidth >= rightWidth && centerWidth > 1) {
            const take = Math.min(excess, centerWidth - 1);
            centerWidth -= take;
        } else if (leftWidth >= rightWidth && leftWidth > 1) {
            const take = Math.min(excess, leftWidth - 1);
            leftWidth -= take;
        } else if (rightWidth > 1) {
            const take = Math.min(excess, rightWidth - 1);
            rightWidth -= take;
        } else {
            break; // All columns at minimum
        }
        colSum = leftWidth + centerWidth + rightWidth;
    }
    return {leftWidth: leftWidth, centerWidth: centerWidth, rightWidth: rightWidth,
        leftX: areaX, centerX: areaX + leftWidth + innerGap,
        rightX: areaX + leftWidth + innerGap + centerWidth + innerGap};
}
