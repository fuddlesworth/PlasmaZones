// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Centered Master
// @builtinId centered-master
// @description Master windows centered with stacks on both sides
// @producesOverlappingZones false
// @supportsMasterCount true
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 7
// @minimumWindows 1
// @zoneNumberDisplay all
// @centerLayout true
// @masterZoneIndex 0
// @supportsMemory false

/**
 * Centered Master layout.
 *
 * Master windows in center column with stacks wrapping both sides.
 * Unlike ThreeColumn (which always has exactly 1 center window),
 * CenteredMaster supports multiple masters stacked vertically in
 * the center column.
 *
 * 1 window:  full screen
 * 2 windows: master left, stack right (2-column)
 * 3+ windows: left stack, center masters, right stack (3-column)
 *   Stack windows interleave: even indices -> left, odd -> right
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];

    const area = params.area;
    const gap = params.innerGap || 0;
    const minSizes = params.minSizes || [];

    const masterCount = Math.max(1, Math.min(params.masterCount || 1, count));
    const stackCount = count - masterCount;
    const splitRatio = params.splitRatio;

    // Case 1: Only masters — stack vertically, full width
    if (stackCount === 0) {
        const masterMinH = extractMinHeights(minSizes, masterCount);
        const masterHeights = (masterMinH.length === 0)
            ? distributeWithGaps(area.height, masterCount, gap)
            : distributeWithMinSizes(area.height, masterCount, gap, masterMinH);
        const zones = [];
        let currentY = area.y;
        for (let i = 0; i < masterCount; i++) {
            zones.push({x: area.x, y: currentY, width: area.width, height: masterHeights[i]});
            currentY += masterHeights[i] + gap;
        }
        return zones;
    }

    // Case 2: One stack window — 2-column layout (masters left, stack right)
    if (stackCount === 1) {
        const contentWidth = area.width - gap;
        let masterWidth = Math.floor(contentWidth * splitRatio);
        let stackWidth = contentWidth - masterWidth;

        // Min-width clamping
        if (minSizes.length > 0) {
            const minMW = (0 < minSizes.length) ? (minSizes[0].w || 0) : 0;
            const minSW = (masterCount < minSizes.length) ? (minSizes[masterCount].w || 0) : 0;
            const solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = solved.first;
            stackWidth = solved.second;
        }

        // Masters stacked vertically on left
        const masterMinH = extractMinHeights(minSizes, masterCount);
        const mHeights = (masterMinH.length === 0)
            ? distributeWithGaps(area.height, masterCount, gap)
            : distributeWithMinSizes(area.height, masterCount, gap, masterMinH);
        const zones = [];
        let cy = area.y;
        for (let i = 0; i < masterCount; i++) {
            zones.push({x: area.x, y: cy, width: masterWidth, height: mHeights[i]});
            cy += mHeights[i] + gap;
        }

        // Single stack on right
        zones.push({x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height});
        return zones;
    }

    // Case 3: 3-column layout — left stack, center masters, right stack
    const leftCount = Math.ceil(stackCount / 2);
    const rightCount = stackCount - leftCount;

    const contentWidth = area.width - 2 * gap;

    if (contentWidth <= 0) {
        return fillArea(area, count);
    }

    // Build stackIsLeft mapping (even -> left, odd -> right)
    const stackIsLeft = buildStackIsLeft(stackCount, leftCount, rightCount);

    // Compute per-column minimum widths
    const minCenterWidth = extractRegionMaxMin(minSizes, 0, masterCount, 'w');
    const sideMinW = (minSizes.length > 0)
        ? interleaveMinWidths(minSizes, stackIsLeft, stackCount, masterCount)
        : {minLeftWidth: 0, minRightWidth: 0};
    const minLeftWidth = sideMinW.minLeftWidth;
    const minRightWidth = sideMinW.minRightWidth;

    const cols = solveThreeColumn(area.x, contentWidth, gap, splitRatio,
                                minLeftWidth, minCenterWidth, minRightWidth);

    const leftWidth = cols.leftWidth;
    const centerWidth = cols.centerWidth;
    const rightWidth = cols.rightWidth;
    const leftX = cols.leftX;
    const centerX = cols.centerX;
    const rightX = cols.rightX;

    // Calculate per-column minimum heights
    const masterMinH = [];
    if (minSizes.length > 0) {
        for (let i = 0; i < masterCount; i++) {
            masterMinH.push((i < minSizes.length) ? (minSizes[i].h || 0) : 0);
        }
    }
    const sideMinH = (minSizes.length > 0)
        ? interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, masterCount)
        : {leftMinH: [], rightMinH: []};
    const leftMinH = sideMinH.leftMinH;
    const rightMinH = sideMinH.rightMinH;

    const masterHeights = masterMinH.length === 0
        ? distributeWithGaps(area.height, masterCount, gap)
        : distributeWithMinSizes(area.height, masterCount, gap, masterMinH);
    const leftHeights = leftMinH.length === 0
        ? distributeWithGaps(area.height, leftCount, gap)
        : distributeWithMinSizes(area.height, leftCount, gap, leftMinH);
    let rightHeights = [];
    if (rightCount > 0) {
        rightHeights = rightMinH.length === 0
            ? distributeWithGaps(area.height, rightCount, gap)
            : distributeWithMinSizes(area.height, rightCount, gap, rightMinH);
    }

    // Masters in center column (stacked vertically)
    const zones = [];
    let currentY = area.y;
    for (let i = 0; i < masterCount; i++) {
        zones.push({x: centerX, y: currentY, width: centerWidth, height: masterHeights[i]});
        currentY += masterHeights[i] + gap;
    }

    // Assign stack windows using precomputed side mapping
    assignInterleavedStacks(zones, stackIsLeft, stackCount,
                            leftX, rightX, leftWidth, rightWidth,
                            leftHeights, rightHeights, area.y, gap);

    return zones;
}
