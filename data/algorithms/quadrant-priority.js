// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Quadrant Priority
// @builtinId quadrant-priority
// @description First window gets a large corner; rest fill the L-shaped remainder
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.6
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Quadrant Priority layout: first window occupies a large top-left quadrant.
 * Remaining windows fill the L-shaped remainder — right column and bottom row.
 * splitRatio controls the master quadrant size (both width and height fraction).
 *
 * Distribution: right column gets ceil((n-1)/2) windows, bottom row gets floor((n-1)/2).
 * Right column height constrained to master height when bottom row exists.
 * Bottom row spans the full area width.
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

    // Quadrant Priority: ceil/floor distribution, bottom row spans full width,
    // right column constrained to master height
    return lShapeLayout(area, count, gap, splitRatio, "ceil-floor", "full", "master");
}
