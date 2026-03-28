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
    var minCenterWidth = 0;
    var minLeftWidth = 0;
    var minRightWidth = 0;
    if (minSizes.length > 0) {
        minCenterWidth = (minSizes[0].w > 0) ? minSizes[0].w : 0;
        var li = 0;
        var ri = 0;
        for (var i = 0; i < stackCount; i++) {
            var zoneIdx = i + 1; // skip center at index 0
            var mw = (zoneIdx < minSizes.length && minSizes[zoneIdx].w > 0) ? minSizes[zoneIdx].w : 0;
            if (i % 2 === 0 && li < leftCount) {
                if (mw > minLeftWidth) minLeftWidth = mw;
                li++;
            } else if (ri < rightCount) {
                if (mw > minRightWidth) minRightWidth = mw;
                ri++;
            } else if (li < leftCount) {
                if (mw > minLeftWidth) minLeftWidth = mw;
                li++;
            }
        }
    }

    var cols = solveThreeColumn(area.x, contentWidth, gap, splitRatio,
                                minLeftWidth, minCenterWidth, minRightWidth);

    var leftWidth = cols.leftWidth;
    var centerWidth = cols.centerWidth;
    var rightWidth = cols.rightWidth;
    var leftX = cols.leftX;
    var centerX = cols.centerX;
    var rightX = cols.rightX;

    // Build per-column min heights from minSizes interleaving order
    var leftMinHeights = [];
    var rightMinHeights = [];
    if (minSizes.length > 0) {
        for (var i = 0; i < leftCount; i++) leftMinHeights.push(0);
        for (var i = 0; i < rightCount; i++) rightMinHeights.push(0);
        var li = 0;
        var ri = 0;
        for (var i = 0; i < stackCount; i++) {
            var zoneIdx = i + 1;
            var mh = (zoneIdx < minSizes.length && minSizes[zoneIdx].h > 0) ? minSizes[zoneIdx].h : 0;
            if (i % 2 === 0 && li < leftCount) {
                leftMinHeights[li] = mh;
                li++;
            } else if (ri < rightCount) {
                rightMinHeights[ri] = mh;
                ri++;
            } else if (li < leftCount) {
                leftMinHeights[li] = mh;
                li++;
            }
        }
    }

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
    var leftIdx = 0;
    var rightIdx = 0;
    var leftY = area.y;
    var rightY = area.y;

    for (var i = 0; i < stackCount; i++) {
        if (i % 2 === 0 && leftIdx < leftCount) {
            zones.push({x: leftX, y: leftY, width: leftWidth, height: leftHeights[leftIdx]});
            leftY += leftHeights[leftIdx] + gap;
            leftIdx++;
        } else if (rightIdx < rightCount) {
            zones.push({x: rightX, y: rightY, width: rightWidth, height: rightHeights[rightIdx]});
            rightY += rightHeights[rightIdx] + gap;
            rightIdx++;
        } else if (leftIdx < leftCount) {
            zones.push({x: leftX, y: leftY, width: leftWidth, height: leftHeights[leftIdx]});
            leftY += leftHeights[leftIdx] + gap;
            leftIdx++;
        }
    }

    return zones;
}
