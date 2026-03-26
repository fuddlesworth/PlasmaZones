// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Zen
// @description Centered column with margins — focused, distraction-free layout
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.6
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all

/**
 * Zen layout: all windows share the same width (splitRatio of screen) and
 * stack vertically in a centered column with gaps. Screen edges are empty
 * margins for a distraction-free experience.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    var area = params.area;
    var gap = params.innerGap || 0;

    if (count <= 0) return [];

    var splitRatio = params.splitRatio > 0 ? params.splitRatio : 0.6;
    var columnWidth = Math.round(area.width * splitRatio);
    var offsetX = area.x + Math.round((area.width - columnWidth) / 2);

    if (count === 1) {
        return [{ x: offsetX, y: area.y, width: columnWidth, height: area.height }];
    }

    var totalGaps = (count - 1) * gap;
    var tileHeight = Math.round((area.height - totalGaps) / count);

    var zones = [];

    for (var i = 0; i < count; i++) {
        var y = area.y + i * (tileHeight + gap);
        var h = (i === count - 1)
            ? (area.y + area.height - y)
            : tileHeight;

        zones.push({
            x: offsetX,
            y: y,
            width: columnWidth,
            height: h
        });
    }

    return zones;
}
