// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Columns",
    id: "columns",
    description: "Equal-width vertical columns",
    producesOverlappingZones: false,
    supportsMasterCount: false,
    supportsSplitRatio: false,
    defaultMaxWindows: 4,
    minimumWindows: 1,
    zoneNumberDisplay: "all",
    supportsMemory: false
};

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
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }
    return equalColumnsLayout(area, count, params.innerGap, params.minSizes);
}
