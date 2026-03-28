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
 * Note: single-window case is handled by the C++ ScriptedAlgorithm wrapper,
 * so this function only receives count >= 2.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;

    var zones = [];
    for (var i = 0; i < count; i++) {
        zones.push({ x: area.x, y: area.y, width: area.width, height: area.height });
    }
    return zones;
}
