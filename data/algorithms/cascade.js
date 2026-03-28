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
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;

    // Single window: fill area (defensive — C++ ScriptedAlgorithm short-circuits
    // single-window before calling JS, but guard against standalone use)
    if (count === 1) {
        return [{x: area.x, y: area.y, width: area.width, height: area.height}];
    }

    // Clamp splitRatio to cascade-specific range (C++ wrapper clamps to 0.1-0.9,
    // but cascade needs tighter bounds)
    const offsetRatio = Math.max(0.02, Math.min(0.4, params.splitRatio));

    const offsetX = Math.max(20, Math.round(area.width * offsetRatio / (count - 1)));
    const offsetY = Math.max(20, Math.round(area.height * offsetRatio / (count - 1)));

    // Each window is sized to fill the area minus the total cascade offset
    const totalOffsetX = offsetX * (count - 1);
    const totalOffsetY = offsetY * (count - 1);
    const winWidth = Math.max(100, area.width - totalOffsetX);
    const winHeight = Math.max(100, area.height - totalOffsetY);

    const zones = [];
    for (let i = 0; i < count; i++) {
        const x = area.x + offsetX * i;
        const y = area.y + offsetY * i;
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
