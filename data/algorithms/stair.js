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
// Stair-specific ratio bounds (tighter than PZ_MIN_SPLIT/PZ_MAX_SPLIT)
const StairMinSizeRatio = 0.3;
const StairMaxSizeRatio = 0.8;

function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;

    // Degenerate screen — fill area (consistent with non-overlapping algorithms)
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    // Tighter than PZ_MIN_SPLIT/PZ_MAX_SPLIT: below StairMinSizeRatio windows are too small
    const sizeRatio = Math.max(StairMinSizeRatio, Math.min(StairMaxSizeRatio, params.splitRatio));

    // All windows are the same size (min 100px to preserve the C++ staircase visual minimum)
    const StairMinWindowPx = 100;
    let winWidth = Math.max(StairMinWindowPx, Math.floor(area.width * sizeRatio));
    let winHeight = Math.max(StairMinWindowPx, Math.floor(area.height * sizeRatio));
    winWidth = Math.min(winWidth, area.width);
    winHeight = Math.min(winHeight, area.height);

    // Diagonal offset distributes the remaining space evenly across steps
    const totalOffsetX = Math.max(0, area.width - winWidth);
    const totalOffsetY = Math.max(0, area.height - winHeight);
    const stepX = Math.floor(totalOffsetX / (count - 1));
    const stepY = Math.floor(totalOffsetY / (count - 1));

    const zones = [];
    for (let i = 0; i < count; i++) {
        const x = area.x + stepX * i;
        const y = area.y + stepY * i;
        let w = winWidth;
        let h = winHeight;

        // Apply per-window minimum sizes, clamped to remaining space at this offset
        const clamped = applyPerWindowMinSize(w, h, params.minSizes, i);
        w = Math.max(1, Math.min(clamped.w, area.x + area.width - x));
        h = Math.max(1, Math.min(clamped.h, area.y + area.height - y));

        zones.push({ x: x, y: y, width: w, height: h });
    }
    return zones;
}
