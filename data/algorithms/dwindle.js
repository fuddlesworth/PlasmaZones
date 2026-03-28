// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Dwindle
// @builtinId dwindle
// @description Each new window gets a smaller split, alternating direction
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Dwindle layout: recursively subdivides space using alternating
 * vertical/horizontal splits. Each window takes the left/top portion
 * of the remaining area.
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
    const minSizes = params.minSizes;

    // splitRatio is clamped inside dwindleLayout()
    return dwindleLayout(area, count, params.splitRatio, gap, minSizes);
}
