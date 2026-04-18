// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Shared three-column layout: center master(s) with left/right side columns.
 * Used by both centered-master (masterCount >= 1) and three-column (masterCount = 1).
 *
 * Stack windows are interleaved between left and right columns using the
 * buildStackIsLeft / assignInterleavedStacks builtins.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - Total window count (masters + stacks, must be >= 3)
 * @param {number} gap - Inner gap between zones
 * @param {number} splitRatio - Already-clamped split ratio
 * @param {number} masterCount - Number of master windows in center column
 * @param {Array} minSizes - Per-window {w, h} minimum sizes (may be empty)
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function threeColumnLayout(area, count, gap, splitRatio, masterCount, minSizes) {
    const stackCount = count - masterCount;
    const leftCount = Math.ceil(stackCount / 2);
    const rightCount = stackCount - leftCount;

    const contentWidth = area.width - 2 * gap;
    if (contentWidth < 3 * PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    // Build left/right interleaving map
    const stackIsLeft = buildStackIsLeft(stackCount, leftCount, rightCount);

    // Compute per-column minimum widths from minSizes
    const minCenterWidth = extractRegionMaxMin(minSizes, 0, masterCount, 'w');
    const sideMinW = (minSizes.length > 0)
        ? interleaveMinWidths(minSizes, stackIsLeft, stackCount, masterCount)
        : {minLeftWidth: 0, minRightWidth: 0};

    const cols = solveThreeColumn(area.x, contentWidth, gap, splitRatio,
                                  sideMinW.minLeftWidth, minCenterWidth, sideMinW.minRightWidth);

    // Compute per-column minimum heights
    const masterMinH = extractMinHeights(minSizes, masterCount);
    const sideMinH = (minSizes.length > 0)
        ? interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, masterCount)
        : {leftMinH: [], rightMinH: []};

    const masterHeights = distributeWithOptionalMins(area.height, masterCount, gap, masterMinH);
    const leftHeights = (leftCount > 0)
        ? distributeWithOptionalMins(area.height, leftCount, gap, sideMinH.leftMinH)
        : [];
    const rightHeights = (rightCount > 0)
        ? distributeWithOptionalMins(area.height, rightCount, gap, sideMinH.rightMinH)
        : [];

    // Masters in center column (stacked vertically)
    const zones = [];
    let currentY = area.y;
    for (let i = 0; i < masterCount; i++) {
        zones.push({x: cols.centerX, y: currentY, width: cols.centerWidth, height: masterHeights[i]});
        currentY += masterHeights[i] + gap;
    }

    // Interleave left and right column windows
    assignInterleavedStacks(zones, stackIsLeft, stackCount,
                            cols.leftX, cols.rightX, cols.leftWidth, cols.rightWidth,
                            leftHeights, rightHeights, area.y, gap);

    return zones;
}
