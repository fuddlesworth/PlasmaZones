// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Tatami
// @description Japanese tatami mat pattern where no four corners ever meet at the same point
// @icon view-grid-symbolic
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 6
// @minimumWindows 1

/**
 * Tatami layout: alternating horizontal and vertical rectangles
 * arranged so that no four corners ever meet at a single point.
 *
 * Windows are placed in pairs on rows. Even-count layouts use a 2x2
 * grid with offset split positions (55%/45%) to break the cross.
 * Odd-count layouts fill pairs first, then the last window spans
 * the full remaining width.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    var area = params.area;
    var gap = params.innerGap || 0;

    if (count <= 0) return [];
    if (count === 1) return [area];

    // Two windows: simple vertical split at 50%
    if (count === 2) {
        var halfW = Math.round((area.width - gap) / 2);
        return [
            { x: area.x, y: area.y, width: halfW, height: area.height },
            { x: area.x + halfW + gap, y: area.y, width: area.width - halfW - gap, height: area.height }
        ];
    }

    var zones = [];
    var rows = Math.ceil(count / 2);
    var rowHeight = Math.round((area.height - gap * (rows - 1)) / rows);
    if (rowHeight < 1) rowHeight = 1;

    for (var row = 0; row < rows; row++) {
        var y = area.y + row * (rowHeight + gap);
        var h = (row === rows - 1) ? (area.y + area.height - y) : rowHeight;
        var windowsInRow = (row === rows - 1 && count % 2 !== 0) ? 1 : 2;

        if (windowsInRow === 1) {
            // Last row, odd window: take full width
            zones.push({ x: area.x, y: y, width: area.width, height: h });
        } else {
            // Offset the split point to prevent four-corner intersections.
            // Even rows split at 55%, odd rows split at 45%.
            var ratio = (row % 2 === 0) ? 0.55 : 0.45;
            var leftW = Math.round((area.width - gap) * ratio);
            var rightW = area.width - gap - leftW;

            zones.push({ x: area.x, y: y, width: leftW, height: h });
            zones.push({ x: area.x + leftW + gap, y: y, width: rightW, height: h });
        }
    }

    return zones;
}
