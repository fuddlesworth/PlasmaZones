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
    var count = params.windowCount;
    if (count <= 0) return [];

    var area = params.area;
    var gap = params.innerGap || 0;
    var minSizes = params.minSizes || [];

    var masterCount = Math.max(1, Math.min(params.masterCount || 1, count));
    var stackCount = count - masterCount;
    var splitRatio = params.splitRatio;

    // Case 1: Only masters — stack vertically, full width
    if (stackCount === 0) {
        var masterHeights = distributeWithGaps(area.height, masterCount, gap);
        var zones = [];
        var currentY = area.y;
        for (var i = 0; i < masterCount; i++) {
            zones.push({x: area.x, y: currentY, width: area.width, height: masterHeights[i]});
            currentY += masterHeights[i] + gap;
        }
        return zones;
    }

    // Case 2: One stack window — 2-column layout (masters left, stack right)
    if (stackCount === 1) {
        var contentWidth = area.width - gap;
        var masterWidth = Math.round(contentWidth * splitRatio);
        var stackWidth = contentWidth - masterWidth;

        // Min-width clamping
        if (minSizes.length > 0) {
            var minMW = (0 < minSizes.length) ? (minSizes[0].w || 0) : 0;
            var minSW = (masterCount < minSizes.length) ? (minSizes[masterCount].w || 0) : 0;
            var solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = solved.first;
            stackWidth = solved.second;
        }

        // Masters stacked vertically on left
        var mHeights = distributeWithGaps(area.height, masterCount, gap);
        var zones = [];
        var cy = area.y;
        for (var i = 0; i < masterCount; i++) {
            zones.push({x: area.x, y: cy, width: masterWidth, height: mHeights[i]});
            cy += mHeights[i] + gap;
        }

        // Single stack on right
        zones.push({x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height});
        return zones;
    }

    // Case 3: 3-column layout — left stack, center masters, right stack
    var leftCount = Math.ceil(stackCount / 2);
    var rightCount = stackCount - leftCount;

    var contentWidth = area.width - 2 * gap;

    if (contentWidth <= 0) {
        // Degenerate: screen too narrow
        var zones = [];
        for (var i = 0; i < count; i++) {
            zones.push({x: area.x, y: area.y, width: area.width, height: area.height});
        }
        return zones;
    }

    // Build stackIsLeft mapping (even -> left, odd -> right)
    var stackIsLeft = [];
    var li = 0;
    var ri = 0;
    for (var i = 0; i < stackCount; i++) {
        if (i % 2 === 0 && li < leftCount) {
            stackIsLeft.push(true);
            li++;
        } else if (ri < rightCount) {
            stackIsLeft.push(false);
            ri++;
        } else {
            stackIsLeft.push(true);
            li++;
        }
    }

    // Compute per-column minimum widths
    var minCenterWidth = 0;
    var minLeftWidth = 0;
    var minRightWidth = 0;
    if (minSizes.length > 0) {
        for (var i = 0; i < masterCount && i < minSizes.length; i++) {
            minCenterWidth = Math.max(minCenterWidth, minSizes[i].w || 0);
        }
        for (var i = 0; i < stackCount; i++) {
            var zoneIdx = masterCount + i;
            if (zoneIdx < minSizes.length) {
                if (stackIsLeft[i]) {
                    minLeftWidth = Math.max(minLeftWidth, minSizes[zoneIdx].w || 0);
                } else {
                    minRightWidth = Math.max(minRightWidth, minSizes[zoneIdx].w || 0);
                }
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

    // Calculate per-column minimum heights
    var masterMinH = [];
    var leftMinH = [];
    var rightMinH = [];
    if (minSizes.length > 0) {
        for (var i = 0; i < masterCount; i++) {
            masterMinH.push((i < minSizes.length) ? (minSizes[i].h || 0) : 0);
        }
        for (var i = 0; i < stackCount; i++) {
            var mh = (masterCount + i < minSizes.length) ? (minSizes[masterCount + i].h || 0) : 0;
            if (stackIsLeft[i]) {
                leftMinH.push(mh);
            } else {
                rightMinH.push(mh);
            }
        }
    }

    var masterHeights = masterMinH.length === 0
        ? distributeWithGaps(area.height, masterCount, gap)
        : distributeWithMinSizes(area.height, masterCount, gap, masterMinH);
    var leftHeights = leftMinH.length === 0
        ? distributeWithGaps(area.height, leftCount, gap)
        : distributeWithMinSizes(area.height, leftCount, gap, leftMinH);
    var rightHeights = [];
    if (rightCount > 0) {
        rightHeights = rightMinH.length === 0
            ? distributeWithGaps(area.height, rightCount, gap)
            : distributeWithMinSizes(area.height, rightCount, gap, rightMinH);
    }

    // Masters in center column (stacked vertically)
    var zones = [];
    var currentY = area.y;
    for (var i = 0; i < masterCount; i++) {
        zones.push({x: centerX, y: currentY, width: centerWidth, height: masterHeights[i]});
        currentY += masterHeights[i] + gap;
    }

    // Assign stack windows using precomputed side mapping
    var leftIdx = 0;
    var rightIdx = 0;
    var leftY = area.y;
    var rightY = area.y;

    for (var i = 0; i < stackCount; i++) {
        if (stackIsLeft[i]) {
            zones.push({x: leftX, y: leftY, width: leftWidth, height: leftHeights[leftIdx]});
            leftY += leftHeights[leftIdx] + gap;
            leftIdx++;
        } else {
            zones.push({x: rightX, y: rightY, width: rightWidth, height: rightHeights[rightIdx]});
            rightY += rightHeights[rightIdx] + gap;
            rightIdx++;
        }
    }

    return zones;
}
