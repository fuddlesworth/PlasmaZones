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
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;
    var gap = params.innerGap || 0;
    var splitRatio = params.splitRatio;
    var minSizes = params.minSizes || [];

    // Two windows: simple left/right split
    if (count === 2) {
        var ratio = Math.max(PZ_MIN_SPLIT, Math.min(splitRatio, PZ_MAX_SPLIT));
        var contentWidth = Math.max(1, area.width - gap);
        var masterWidth = Math.floor(contentWidth * ratio);
        var stackWidth = contentWidth - masterWidth;

        // Joint min-width solve for 2-window case
        if (minSizes.length > 0) {
            var minMW = (0 < minSizes.length && minSizes[0].w > 0) ? minSizes[0].w : 0;
            var minSW = (1 < minSizes.length && minSizes[1].w > 0) ? minSizes[1].w : 0;
            var solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = solved.first;
            stackWidth = solved.second;
        }

        return [
            {x: area.x, y: area.y, width: masterWidth, height: area.height},
            {x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height}
        ];
    }

    // Fall back to equal columns if screen is too narrow for three columns
    if (count >= 3 && area.width < 3 * PZ_MIN_ZONE_SIZE) {
        var widths = distributeWithGaps(area.width, count, gap);
        var zones = [];
        var x = area.x;
        for (var i = 0; i < count; i++) {
            zones.push({x: x, y: area.y, width: widths[i], height: area.height});
            x += widths[i] + gap;
        }
        return zones;
    }

    // Three or more windows: true three-column layout
    var contentWidth = area.width - 2 * gap;

    if (contentWidth <= 0) {
        // Degenerate case: screen too narrow for 3 columns with gaps
        var zones = [];
        for (var i = 0; i < count; i++) {
            zones.push({x: area.x, y: area.y, width: area.width, height: area.height});
        }
        return zones;
    }

    // Count windows for each column (excluding master)
    var stackCount = count - 1;
    var leftCount = Math.ceil(stackCount / 2); // Left gets extra if odd
    var rightCount = stackCount - leftCount;

    // Compute per-column minimum widths from minSizes
    // Zone ordering: [center(0), left1(1), right1(2), left2(3), right2(4), ...]
    var stackIsLeft = buildStackIsLeft(stackCount, leftCount, rightCount);
    var minCenterWidth = 0;
    if (minSizes.length > 0) {
        minCenterWidth = (minSizes[0].w > 0) ? minSizes[0].w : 0;
    }
    var sideMinW = (minSizes.length > 0)
        ? interleaveMinWidths(minSizes, stackIsLeft, stackCount, 1)
        : {minLeftWidth: 0, minRightWidth: 0};
    var minLeftWidth = sideMinW.minLeftWidth;
    var minRightWidth = sideMinW.minRightWidth;

    var cols = solveThreeColumn(area.x, contentWidth, gap, splitRatio,
                                minLeftWidth, minCenterWidth, minRightWidth);

    var leftWidth = cols.leftWidth;
    var centerWidth = cols.centerWidth;
    var rightWidth = cols.rightWidth;
    var leftX = cols.leftX;
    var centerX = cols.centerX;
    var rightX = cols.rightX;

    // Build per-column min heights from minSizes interleaving order
    var sideMinH = (minSizes.length > 0)
        ? interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, 1)
        : {leftMinH: [], rightMinH: []};
    var leftMinHeights = sideMinH.leftMinH;
    var rightMinHeights = sideMinH.rightMinH;

    // Calculate heights with gaps between vertically stacked zones
    var leftHeights = [];
    if (leftCount > 0) {
        leftHeights = (leftMinHeights.length === 0)
            ? distributeWithGaps(area.height, leftCount, gap)
            : distributeWithMinSizes(area.height, leftCount, gap, leftMinHeights);
    }

    var rightHeights = [];
    if (rightCount > 0) {
        rightHeights = (rightMinHeights.length === 0)
            ? distributeWithGaps(area.height, rightCount, gap)
            : distributeWithMinSizes(area.height, rightCount, gap, rightMinHeights);
    }

    // First zone: center/master (full height)
    var zones = [];
    zones.push({x: centerX, y: area.y, width: centerWidth, height: area.height});

    // Interleave left and right column windows
    assignInterleavedStacks(zones, stackIsLeft, stackCount,
                            leftX, rightX, leftWidth, rightWidth,
                            leftHeights, rightHeights, area.y, gap);

    return zones;
}
