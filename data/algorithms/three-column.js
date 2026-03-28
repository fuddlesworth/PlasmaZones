// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Three Column
// @builtinId three-column
// @description Master window centered with columns on each side
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @centerLayout true
// @masterZoneIndex 0
// @supportsMemory false

/**
 * Three Column layout: center master with left/right side columns.
 * Stack windows are interleaved between left and right columns.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const gap = params.innerGap || 0;
    const splitRatio = params.splitRatio;
    const minSizes = params.minSizes || [];

    // Two windows: simple left/right split
    if (count === 2) {
        const ratio = Math.max(PZ_MIN_SPLIT, Math.min(splitRatio, PZ_MAX_SPLIT));
        const contentWidth = Math.max(1, area.width - gap);
        let masterWidth = Math.floor(contentWidth * ratio);
        let stackWidth = contentWidth - masterWidth;

        // Joint min-width solve for 2-window case
        if (minSizes.length > 0) {
            const minMW = (0 < minSizes.length && minSizes[0].w > 0) ? minSizes[0].w : 0;
            const minSW = (1 < minSizes.length && minSizes[1].w > 0) ? minSizes[1].w : 0;
            const solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = solved.first;
            stackWidth = solved.second;
        }

        return [
            {x: area.x, y: area.y, width: masterWidth, height: area.height},
            {x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height}
        ];
    }

    // Fall back to equal columns if screen is too narrow for three columns
    if (count >= 3 && area.width < 3 * PZ_MIN_ZONE_SIZE + 2 * gap) {
        const widths = distributeWithGaps(area.width, count, gap);
        const zones = [];
        let x = area.x;
        for (let i = 0; i < count; i++) {
            zones.push({x: x, y: area.y, width: widths[i], height: area.height});
            x += widths[i] + gap;
        }
        return zones;
    }

    // Three or more windows: true three-column layout
    const contentWidth = area.width - 2 * gap;

    if (contentWidth <= 0) {
        return fillArea(area, count);
    }

    // Count windows for each column (excluding master)
    const stackCount = count - 1;
    const leftCount = Math.ceil(stackCount / 2); // Left gets extra if odd
    const rightCount = stackCount - leftCount;

    // Compute per-column minimum widths from minSizes
    // Zone ordering: [center(0), left1(1), right1(2), left2(3), right2(4), ...]
    const stackIsLeft = buildStackIsLeft(stackCount, leftCount, rightCount);
    let minCenterWidth = 0;
    if (minSizes.length > 0) {
        minCenterWidth = (minSizes[0].w > 0) ? minSizes[0].w : 0;
    }
    const sideMinW = (minSizes.length > 0)
        ? interleaveMinWidths(minSizes, stackIsLeft, stackCount, 1)
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

    // Build per-column min heights from minSizes interleaving order
    const sideMinH = (minSizes.length > 0)
        ? interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, 1)
        : {leftMinH: [], rightMinH: []};
    const leftMinHeights = sideMinH.leftMinH;
    const rightMinHeights = sideMinH.rightMinH;

    // Calculate heights with gaps between vertically stacked zones
    let leftHeights = [];
    if (leftCount > 0) {
        leftHeights = (leftMinHeights.length === 0)
            ? distributeWithGaps(area.height, leftCount, gap)
            : distributeWithMinSizes(area.height, leftCount, gap, leftMinHeights);
    }

    let rightHeights = [];
    if (rightCount > 0) {
        rightHeights = (rightMinHeights.length === 0)
            ? distributeWithGaps(area.height, rightCount, gap)
            : distributeWithMinSizes(area.height, rightCount, gap, rightMinHeights);
    }

    // First zone: center/master (full height)
    const zones = [];
    zones.push({x: centerX, y: area.y, width: centerWidth, height: area.height});

    // Interleave left and right column windows
    assignInterleavedStacks(zones, stackIsLeft, stackCount,
                            leftX, rightX, leftWidth, rightWidth,
                            leftHeights, rightHeights, area.y, gap);

    return zones;
}
