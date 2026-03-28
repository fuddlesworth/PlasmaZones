// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Corner Master
// @builtinId corner-master
// @description Master window in a corner; rest fill the L-shaped remainder
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.55
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Corner Master layout (see also quadrant-priority.js for a similar L-shape
 * variant with ceil/floor distribution instead of alternating).
 *
 * First window takes the top-left corner. Remaining
 * windows fill the L-shaped remainder — right column (master height) and
 * bottom row (master width).
 *
 * splitRatio controls the master window's width AND height fraction.
 *
 * Distribution for remaining windows:
 * - 2 windows: win2 fills right column (master height)
 * - 3 windows: win2 right column, win3 bottom row
 * - 4+: alternate between right column and bottom row
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */

// Uses injected lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight)

function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const gap = params.innerGap || 0;
    const splitRatio = params.splitRatio;

    // Corner Master: alternate distribution, bottom row uses master width,
    // right column uses master height (respects bottom row when present)
    return lShapeLayout(area, count, gap, splitRatio, "alternate", "master", "master");
}
