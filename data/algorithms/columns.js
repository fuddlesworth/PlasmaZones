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
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const gap = params.innerGap || 0;

    const minWidths = extractMinWidths(params.minSizes || [], count);

    // Calculate column widths with gaps and minimum sizes
    const columnWidths = (minWidths.length === 0)
        ? distributeWithGaps(area.width, count, gap)
        : distributeWithMinSizes(area.width, count, gap, minWidths);

    const zones = [];
    let currentX = area.x;
    for (let i = 0; i < count; i++) {
        zones.push({ x: currentX, y: area.y, width: columnWidths[i], height: area.height });
        currentX += columnWidths[i] + gap;
    }
    return zones;
}
