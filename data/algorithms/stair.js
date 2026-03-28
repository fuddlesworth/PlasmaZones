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
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;

    // Clamp splitRatio to stair-specific range (C++ wrapper clamps to 0.1-0.9,
    // but stair needs tighter bounds)
    const sizeRatio = Math.max(0.3, Math.min(0.8, params.splitRatio));

    // All windows are the same size
    const winWidth = Math.max(100, Math.floor(area.width * sizeRatio));
    const winHeight = Math.max(100, Math.floor(area.height * sizeRatio));

    // Diagonal offset distributes the remaining space evenly across steps
    const totalOffsetX = Math.max(0, area.width - winWidth);
    const totalOffsetY = Math.max(0, area.height - winHeight);
    const stepX = (count > 1) ? Math.floor(totalOffsetX / (count - 1)) : 0;
    const stepY = (count > 1) ? Math.floor(totalOffsetY / (count - 1)) : 0;

    const zones = [];
    for (let i = 0; i < count; i++) {
        const x = area.x + stepX * i;
        const y = area.y + stepY * i;
        let w = winWidth;
        let h = winHeight;

        // Apply per-window minimum sizes
        const clamped = applyPerWindowMinSize(w, h, params.minSizes, i);
        w = clamped.w;
        h = clamped.h;

        zones.push({ x: x, y: y, width: w, height: h });
    }
    return zones;
}
