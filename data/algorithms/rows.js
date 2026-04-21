// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Rows",
    id: "rows",
    description: "Equal-height horizontal rows",
    producesOverlappingZones: false,
    supportsMasterCount: false,
    supportsSplitRatio: false,
    defaultMaxWindows: 4,
    minimumWindows: 1,
    zoneNumberDisplay: "all",
    supportsMemory: false
};

/**
 * Rows layout: equal-height horizontal rows with innerGap spacing.
 * Uses distributeWithGaps / distributeWithMinSizes injected helpers.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }
    const gap = params.innerGap;

    const minHeights = extractMinHeights(params.minSizes, count);

    // Calculate row heights with gaps and minimum sizes
    const rowHeights = distributeWithOptionalMins(area.height, count, gap, minHeights);

    const zones = [];
    let currentY = area.y;
    for (let i = 0; i < count; i++) {
        zones.push({ x: area.x, y: currentY, width: area.width, height: rowHeights[i] });
        currentY += rowHeights[i] + gap;
    }
    return zones;
}
