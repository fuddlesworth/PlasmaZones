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
    const gap = params.innerGap;
    const minSizes = params.minSizes || [];

    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    const masterCount = Math.max(1, Math.min(params.masterCount || 1, count));
    const stackCount = count - masterCount;
    const splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, params.splitRatio));

    // Case 1: Only masters — delegate to shared masterStackLayout (DRY)
    if (stackCount === 0) {
        return masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, false);
    }

    // Case 2: One stack window — 2-column layout (masters left, stack right)
    if (stackCount === 1) {
        // Degenerate: gap consumes all width or height — fall back to stacking
        if (area.width - gap <= 0 || area.height < PZ_MIN_ZONE_SIZE) {
            return fillArea(area, count);
        }
        const contentWidth = area.width - gap;
        let masterWidth = Math.floor(contentWidth * splitRatio);
        let stackWidth = contentWidth - masterWidth;

        // Min-width clamping
        if (minSizes.length > 0) {
            const minMW = minSizes[0].w || 0;
            const minSW = (masterCount < minSizes.length) ? (minSizes[masterCount].w || 0) : 0;
            const solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = Math.max(1, solved.first);
            stackWidth = Math.max(1, solved.second);
        }

        // Masters stacked vertically on left
        const masterMinH = extractMinHeights(minSizes, masterCount);
        const mHeights = distributeWithOptionalMins(area.height, masterCount, gap, masterMinH);
        const zones = [];
        let currentY = area.y;
        for (let i = 0; i < masterCount; i++) {
            zones.push({x: area.x, y: currentY, width: masterWidth, height: mHeights[i]});
            currentY += mHeights[i] + gap;
        }

        // Single stack on right
        zones.push({x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height});
        return zones;
    }

    // Case 3: 3-column layout — left stack, center masters, right stack
    // TODO: Extract shared 3-column layout logic with three-column.js (see PR #259 review)
    const leftCount = Math.ceil(stackCount / 2);
    const rightCount = stackCount - leftCount;

    const contentWidth = area.width - 2 * gap;

    if (contentWidth < 3 * PZ_MIN_ZONE_SIZE) {
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
    const masterMinH = extractMinHeights(minSizes, masterCount);
    const sideMinH = (minSizes.length > 0)
        ? interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, masterCount)
        : {leftMinH: [], rightMinH: []};
    const leftMinH = sideMinH.leftMinH;
    const rightMinH = sideMinH.rightMinH;

    const masterHeights = distributeWithOptionalMins(area.height, masterCount, gap, masterMinH);
    const leftHeights = distributeWithOptionalMins(area.height, leftCount, gap, leftMinH);
    let rightHeights = [];
    if (rightCount > 0) {
        rightHeights = distributeWithOptionalMins(area.height, rightCount, gap, rightMinH);
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
