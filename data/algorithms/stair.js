// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Stair
// @builtinId stair
// @description Stepped staircase arrangement
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Stair layout: overlapping staircase where all windows are the same size
 * and steps are evenly distributed across the remaining space. splitRatio
 * controls window size as a fraction of area dimensions (clamped 0.3-0.8).
 *
 * Overlapping layout -- innerGap intentionally ignored (zones overlap by design).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;

    // Clamp splitRatio to stair-specific range (C++ wrapper clamps to 0.1-0.9,
    // but stair needs tighter bounds)
    var sizeRatio = Math.max(0.3, Math.min(0.8, params.splitRatio));

    // All windows are the same size
    var winWidth = Math.max(100, Math.round(area.width * sizeRatio));
    var winHeight = Math.max(100, Math.round(area.height * sizeRatio));

    // Diagonal offset distributes the remaining space evenly across steps
    var totalOffsetX = area.width - winWidth;
    var totalOffsetY = area.height - winHeight;
    var stepX = (count > 1) ? Math.floor(totalOffsetX / (count - 1)) : 0;
    var stepY = (count > 1) ? Math.floor(totalOffsetY / (count - 1)) : 0;

    var zones = [];
    for (var i = 0; i < count; i++) {
        var x = area.x + stepX * i;
        var y = area.y + stepY * i;
        var w = winWidth;
        var h = winHeight;

        // Apply per-window minimum sizes
        if (params.minSizes && i < params.minSizes.length) {
            if (params.minSizes[i].w > 0) w = Math.max(w, params.minSizes[i].w);
            if (params.minSizes[i].h > 0) h = Math.max(h, params.minSizes[i].h);
        }

        zones.push({ x: x, y: y, width: w, height: h });
    }
    return zones;
}
