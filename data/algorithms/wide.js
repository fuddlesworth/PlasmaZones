// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Wide
// @builtinId wide
// @description Master area on top, remaining windows stacked below
// @producesOverlappingZones false
// @supportsMasterCount true
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @masterZoneIndex 0
// @supportsMemory false

/**
 * Wide layout: master row on top, stack row on bottom.
 * Like MasterStack but rotated 90 degrees.
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

    // Compute per-row minimum heights from minSizes
    var minMasterHeight = 0;
    var minStackHeight = 0;
    if (minSizes.length > 0) {
        for (var i = 0; i < masterCount && i < minSizes.length; i++) {
            var mh = (minSizes[i].h > 0) ? minSizes[i].h : 0;
            if (mh > minMasterHeight) minMasterHeight = mh;
        }
        for (var i = masterCount; i < count && i < minSizes.length; i++) {
            var mh = (minSizes[i].h > 0) ? minSizes[i].h : 0;
            if (mh > minStackHeight) minStackHeight = mh;
        }
    }

    // Calculate master and stack heights
    var masterHeight;
    var stackHeight;

    if (stackCount === 0) {
        // All windows are masters — full height
        masterHeight = area.height;
        stackHeight = 0;
    } else {
        var contentHeight = area.height - gap;
        masterHeight = Math.floor(contentHeight * splitRatio);
        stackHeight = contentHeight - masterHeight;

        // Joint min-height solve
        var solved = solveTwoPart(contentHeight, masterHeight, stackHeight,
                                  minMasterHeight, minStackHeight);
        masterHeight = solved.first;
        stackHeight = solved.second;
    }

    // Extract per-window min widths for each row
    var masterMinWidths = extractMinWidths(minSizes, masterCount);
    var stackMinWidths = [];
    if (minSizes.length > 0) {
        for (var i = 0; i < stackCount; i++) {
            var idx = masterCount + i;
            stackMinWidths.push((idx < minSizes.length && minSizes[idx].w > 0) ? minSizes[idx].w : 0);
        }
    }

    // Calculate zone widths with gaps
    var masterWidths = (masterMinWidths.length === 0)
        ? distributeWithGaps(area.width, masterCount, gap)
        : distributeWithMinSizes(area.width, masterCount, gap, masterMinWidths);

    // Generate master zones (top row, laid out horizontally)
    var zones = [];
    var currentX = area.x;
    for (var i = 0; i < masterCount; i++) {
        zones.push({x: currentX, y: area.y, width: masterWidths[i], height: masterHeight});
        currentX += masterWidths[i] + gap;
    }

    // Generate stack zones (bottom row, laid out horizontally)
    if (stackCount > 0) {
        var stackWidths = (stackMinWidths.length === 0)
            ? distributeWithGaps(area.width, stackCount, gap)
            : distributeWithMinSizes(area.width, stackCount, gap, stackMinWidths);
        var stackY = area.y + masterHeight + gap;

        currentX = area.x;
        for (var i = 0; i < stackCount; i++) {
            zones.push({x: currentX, y: stackY, width: stackWidths[i], height: stackHeight});
            currentX += stackWidths[i] + gap;
        }
    }

    return zones;
}
