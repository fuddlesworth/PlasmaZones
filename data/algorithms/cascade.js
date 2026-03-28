// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Cascade
// @builtinId cascade
// @description Overlapping windows in a diagonal cascade
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.15
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay last
// @supportsMemory false

/**
 * Cascade layout: overlapping diagonal cascade where each window is offset
 * from the previous. splitRatio controls the cascade offset as a fraction
 * of area dimensions (clamped 0.02-0.4).
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

    // Clamp splitRatio to cascade-specific range (C++ wrapper clamps to 0.1-0.9,
    // but cascade needs tighter bounds)
    var offsetRatio = Math.max(0.02, Math.min(0.4, params.splitRatio));

    var offsetX = Math.max(20, Math.round(area.width * offsetRatio / (count - 1)));
    var offsetY = Math.max(20, Math.round(area.height * offsetRatio / (count - 1)));

    // Each window is sized to fill the area minus the total cascade offset
    var totalOffsetX = offsetX * (count - 1);
    var totalOffsetY = offsetY * (count - 1);
    var winWidth = Math.max(100, area.width - totalOffsetX);
    var winHeight = Math.max(100, area.height - totalOffsetY);

    var zones = [];
    for (var i = 0; i < count; i++) {
        var x = area.x + offsetX * i;
        var y = area.y + offsetY * i;
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
