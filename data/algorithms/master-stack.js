// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Master + Stack
// @builtinId master-stack
// @description Large master area with stacked secondary windows
// @producesOverlappingZones false
// @supportsMasterCount true
// @supportsSplitRatio true
// @defaultSplitRatio 0.6
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all
// @masterZoneIndex 0
// @supportsMemory false

/**
 * Classic master-stack tiling: one or more master windows on the left,
 * remaining windows stacked vertically on the right.
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
    var masterCount = params.masterCount;
    var minSizes = params.minSizes || [];

    // Clamp masterCount to [1, count]
    masterCount = Math.max(1, Math.min(masterCount, count));
    var stackCount = count - masterCount;

    // Clamp splitRatio
    splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(splitRatio, PZ_MAX_SPLIT));

    // Compute per-column minimum widths from minSizes
    var minMasterWidth = 0;
    var minStackWidth = 0;
    if (minSizes.length > 0) {
        for (var i = 0; i < masterCount && i < minSizes.length; i++) {
            var mw = (minSizes[i].w > 0) ? minSizes[i].w : 0;
            if (mw > minMasterWidth) minMasterWidth = mw;
        }
        for (var i = masterCount; i < count && i < minSizes.length; i++) {
            var mw = (minSizes[i].w > 0) ? minSizes[i].w : 0;
            if (mw > minStackWidth) minStackWidth = mw;
        }
    }

    // Calculate master and stack widths
    var masterWidth;
    var stackWidth;

    if (stackCount === 0) {
        // All windows are masters — full width
        masterWidth = area.width;
        stackWidth = 0;
    } else {
        var contentWidth = area.width - gap;
        masterWidth = Math.floor(contentWidth * splitRatio);
        stackWidth = contentWidth - masterWidth;

        // Joint min-width solve
        var solved = solveTwoPart(contentWidth, masterWidth, stackWidth,
                                  minMasterWidth, minStackWidth);
        masterWidth = solved.first;
        stackWidth = solved.second;
    }

    // Extract per-window min heights for each column
    var masterMinHeights = extractMinHeights(minSizes, masterCount);
    var stackMinHeights = [];
    if (minSizes.length > 0) {
        for (var i = 0; i < stackCount; i++) {
            var idx = masterCount + i;
            stackMinHeights.push((idx < minSizes.length && minSizes[idx].h > 0) ? minSizes[idx].h : 0);
        }
    }

    // Calculate zone heights with gaps
    var masterHeights = (masterMinHeights.length === 0)
        ? distributeWithGaps(area.height, masterCount, gap)
        : distributeWithMinSizes(area.height, masterCount, gap, masterMinHeights);

    // Generate master zones (left side, stacked vertically)
    var zones = [];
    var currentY = area.y;
    for (var i = 0; i < masterCount; i++) {
        zones.push({x: area.x, y: currentY, width: masterWidth, height: masterHeights[i]});
        currentY += masterHeights[i] + gap;
    }

    // Generate stack zones (right side, stacked vertically)
    if (stackCount > 0) {
        var stackHeights = (stackMinHeights.length === 0)
            ? distributeWithGaps(area.height, stackCount, gap)
            : distributeWithMinSizes(area.height, stackCount, gap, stackMinHeights);
        var stackX = area.x + masterWidth + gap;

        currentY = area.y;
        for (var i = 0; i < stackCount; i++) {
            zones.push({x: stackX, y: currentY, width: stackWidth, height: stackHeights[i]});
            currentY += stackHeights[i] + gap;
        }
    }

    return zones;
}
