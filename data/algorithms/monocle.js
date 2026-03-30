// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Monocle
// @builtinId monocle
// @description One window fullscreen at a time, cycle through others
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay last
// @supportsMemory false

/**
 * Monocle layout: all windows get the same full-area rectangle.
 * Overlapping layout -- innerGap intentionally ignored (zones overlap by design).
 * The focused window is on top; others are stacked behind it.
 *
 * The C++ ScriptedAlgorithm wrapper handles the single-window case before
 * calling this function, but count === 1 is still handled correctly here
 * (fillArea produces a single full-area zone).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;

    return fillArea(area, count);
}
