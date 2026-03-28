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
    return masterStackLayout(
        params.area,
        params.windowCount,
        params.innerGap || 0,
        params.splitRatio,
        params.masterCount,
        params.minSizes || [],
        false
    );
}
