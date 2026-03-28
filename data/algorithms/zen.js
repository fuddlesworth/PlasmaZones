// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Zen
// @builtinId zen
// @description Centered column with margins — focused, distraction-free layout
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.6
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

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
    const gap = params.innerGap || 0;

    const splitRatio = params.splitRatio;
    let columnWidth = Math.round(area.width * splitRatio);
    columnWidth = Math.max(1, columnWidth);
    const offsetX = area.x + Math.round((area.width - columnWidth) / 2);

    const totalGaps = (count - 1) * gap;

    // Degenerate gap: stack all windows when gaps exceed available height
    if (totalGaps >= area.height) {
        const stacked = [];
        for (let j = 0; j < count; j++) {
            stacked.push({ x: offsetX, y: area.y, width: columnWidth, height: area.height });
        }
        return stacked;
    }

    // Use injected distributeEvenly helper for vertical stacking
    const slots = distributeEvenly(area.y, area.height, count, gap);
    const zones = [];
    for (let i = 0; i < slots.length; i++) {
        zones.push({ x: offsetX, y: slots[i].pos, width: columnWidth, height: slots[i].size });
    }
    return zones;
}
