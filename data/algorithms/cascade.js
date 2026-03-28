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
    const offsetRatio = Math.max(PZ_MIN_SPLIT, Math.min(0.4, params.splitRatio));

    // Initial minimum of 20px per step; may be reduced by maxOffset clamp below
    // when window minimum sizes constrain the available cascade space.
    let offsetX = Math.max(20, Math.floor(area.width * offsetRatio / (count - 1)));
    let offsetY = Math.max(20, Math.floor(area.height * offsetRatio / (count - 1)));

    // Each window is sized to fill the area minus the total cascade offset
    const totalOffsetX = offsetX * (count - 1);
    const totalOffsetY = offsetY * (count - 1);
    const winWidth = Math.max(100, area.width - totalOffsetX);
    const winHeight = Math.max(100, area.height - totalOffsetY);

    // Clamp offsets so last window stays within area
    const maxOffsetX = Math.max(1, Math.floor((area.width - winWidth) / Math.max(1, count - 1)));
    const maxOffsetY = Math.max(1, Math.floor((area.height - winHeight) / Math.max(1, count - 1)));
    offsetX = Math.min(offsetX, maxOffsetX);
    offsetY = Math.min(offsetY, maxOffsetY);

    const zones = [];
    for (let i = 0; i < count; i++) {
        const x = area.x + offsetX * i;
        const y = area.y + offsetY * i;
        let w = winWidth;
        let h = winHeight;

        // Apply per-window minimum sizes, clamped to remaining space at this offset
        const clamped = applyPerWindowMinSize(w, h, params.minSizes || [], i);
        w = Math.min(clamped.w, area.x + area.width - x);
        h = Math.min(clamped.h, area.y + area.height - y);

        zones.push({ x: x, y: y, width: w, height: h });
    }
    return zones;
}
