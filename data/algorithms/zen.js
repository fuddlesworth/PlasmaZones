// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Zen",
    builtinId: "zen",
    description: "Centered column with margins — focused, distraction-free layout",
    producesOverlappingZones: false,
    supportsMasterCount: false,
    supportsSplitRatio: true,
    defaultSplitRatio: 0.6,
    defaultMaxWindows: 4,
    minimumWindows: 1,
    zoneNumberDisplay: "all",
    supportsMemory: false
};

/**
 * Zen layout: all windows share the same width (splitRatio of screen) and
 * stack vertically in a centered column with gaps. Screen edges are empty
 * margins for a distraction-free experience.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }
    const gap = params.innerGap;

    const splitRatio = clampSplitRatio(params.splitRatio);
    let columnWidth = Math.floor(area.width * splitRatio);
    columnWidth = Math.max(1, columnWidth);
    const offsetX = area.x + Math.floor((area.width - columnWidth) / 2);

    const totalGaps = (count - 1) * gap;

    // Degenerate gap: stack all windows when gaps exceed available height
    if (totalGaps >= area.height) {
        return fillRegion(offsetX, area.y, columnWidth, area.height, count);
    }

    // Use injected distributeEvenly helper for vertical stacking
    const slots = distributeEvenly(area.y, area.height, count, gap);
    const zones = [];
    for (let i = 0; i < slots.length; i++) {
        zones.push({ x: offsetX, y: slots[i].pos, width: columnWidth, height: slots[i].size });
    }
    return zones;
}
