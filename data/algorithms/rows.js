// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Rows
// @builtinId rows
// @description Equal-height horizontal rows
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Rows layout: equal-height horizontal rows with innerGap spacing.
 * Uses distributeWithGaps / distributeWithMinSizes injected helpers.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;
    var gap = params.innerGap || 0;

    // Extract per-window minimum heights
    var minHeights = [];
    var hasMinSizes = params.minSizes && params.minSizes.length > 0;
    if (hasMinSizes) {
        for (var j = 0; j < count; j++) {
            minHeights.push((j < params.minSizes.length && params.minSizes[j].h > 0)
                ? params.minSizes[j].h : 0);
        }
    }

    // Calculate row heights with gaps and minimum sizes
    var rowHeights = (minHeights.length === 0)
        ? distributeWithGaps(area.height, count, gap)
        : distributeWithMinSizes(area.height, count, gap, minHeights);

    var zones = [];
    var currentY = area.y;
    for (var i = 0; i < count; i++) {
        zones.push({ x: area.x, y: currentY, width: area.width, height: rowHeights[i] });
        currentY += rowHeights[i] + gap;
    }
    return zones;
}
