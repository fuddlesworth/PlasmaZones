// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Columns
// @builtinId columns
// @description Equal-width vertical columns
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Columns layout: equal-width vertical columns with innerGap spacing.
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

    // Extract per-window minimum widths
    var minWidths = [];
    var hasMinSizes = params.minSizes && params.minSizes.length > 0;
    if (hasMinSizes) {
        for (var j = 0; j < count; j++) {
            minWidths.push((j < params.minSizes.length && params.minSizes[j].w > 0)
                ? params.minSizes[j].w : 0);
        }
    }

    // Calculate column widths with gaps and minimum sizes
    var columnWidths = (minWidths.length === 0)
        ? distributeWithGaps(area.width, count, gap)
        : distributeWithMinSizes(area.width, count, gap, minWidths);

    var zones = [];
    var currentX = area.x;
    for (var i = 0; i < count; i++) {
        zones.push({ x: currentX, y: area.y, width: columnWidths[i], height: area.height });
        currentX += columnWidths[i] + gap;
    }
    return zones;
}
