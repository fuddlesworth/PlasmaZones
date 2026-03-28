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
// Cascade-specific ratio bounds (tighter than PZ_MIN_SPLIT/PZ_MAX_SPLIT)
const CascadeMinOffsetRatio = 0.02;
const CascadeMaxOffsetRatio = 0.4;
const CascadeMinOffsetPx = 20;   // Minimum per-step offset in pixels
const CascadeMinWindowPx = 100;  // Minimum window dimension in pixels

function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;

    // Degenerate screen — fill area (consistent with non-overlapping algorithms)
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    // Clamp splitRatio to cascade-specific range, tighter than the
    // global PZ_MIN_SPLIT/PZ_MAX_SPLIT used by non-overlapping algorithms.
    const offsetRatio = Math.max(CascadeMinOffsetRatio, Math.min(CascadeMaxOffsetRatio, params.splitRatio));

    // Initial minimum of CascadeMinOffsetPx per step; may be reduced by maxOffset
    // clamp below when window minimum sizes constrain the available cascade space.
    let offsetX = Math.max(CascadeMinOffsetPx, Math.floor(area.width * offsetRatio / (count - 1)));
    let offsetY = Math.max(CascadeMinOffsetPx, Math.floor(area.height * offsetRatio / (count - 1)));

    // Each window is sized to fill the area minus the total cascade offset,
    // clamped to both the minimum window size and the actual area dimensions.
    const totalOffsetX = offsetX * (count - 1);
    const totalOffsetY = offsetY * (count - 1);
    const winWidth = Math.min(Math.max(CascadeMinWindowPx, area.width - totalOffsetX), area.width);
    const winHeight = Math.min(Math.max(CascadeMinWindowPx, area.height - totalOffsetY), area.height);

    // Clamp offsets so last window stays within area.
    // When winWidth >= area.width (small screen), maxOffset is 0 — windows stack at origin.
    const maxOffsetX = Math.max(0, Math.floor((area.width - winWidth) / Math.max(1, count - 1)));
    const maxOffsetY = Math.max(0, Math.floor((area.height - winHeight) / Math.max(1, count - 1)));
    offsetX = Math.min(offsetX, maxOffsetX);
    offsetY = Math.min(offsetY, maxOffsetY);

    const zones = [];
    for (let i = 0; i < count; i++) {
        const x = area.x + offsetX * i;
        const y = area.y + offsetY * i;
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
