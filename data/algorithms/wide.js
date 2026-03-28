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
    // splitRatio is clamped inside masterStackLayout()
    return masterStackLayout(
        params.area,
        params.windowCount,
        params.innerGap,
        params.splitRatio,
        params.masterCount || 1,
        params.minSizes || [],
        true
    );
}
