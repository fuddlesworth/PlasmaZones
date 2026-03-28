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
    if (params.windowCount <= 0) return [];
    // splitRatio is clamped inside masterStackLayout()
    return masterStackLayout(
        params.area,
        params.windowCount,
        params.innerGap,
        params.splitRatio,
        params.masterCount || 1,
        params.minSizes,
        false
    );
}
