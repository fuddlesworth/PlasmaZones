// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Tatami
// @description Japanese tatami mat pattern where no four corners ever meet at the same point
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 6
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

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
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const gap = params.innerGap || 0;

    // Two windows: simple vertical split at 50%
    if (count === 2) {
        const halfW = Math.round((area.width - gap) / 2);
        return [
            { x: area.x, y: area.y, width: halfW, height: area.height },
            { x: area.x + halfW + gap, y: area.y, width: area.width - halfW - gap, height: area.height }
        ];
    }

    const zones = [];
    const rows = Math.ceil(count / 2);
    let rowHeight = Math.round((area.height - gap * (rows - 1)) / rows);
    if (rowHeight < 1) rowHeight = 1;

    // Degenerate case: gaps consume all available height
    if (area.height - gap * (rows - 1) <= 0) {
        var fallback = [];
        for (var i = 0; i < count; i++) {
            fallback.push({ x: area.x, y: area.y, width: area.width, height: area.height });
        }
        return fallback;
    }

    var EVEN_ROW_RATIO = 0.55; // Offset to prevent four-corner intersections
    var ODD_ROW_RATIO = 0.45;

    for (let row = 0; row < rows; row++) {
        const y = area.y + row * (rowHeight + gap);
        const h = (row === rows - 1) ? Math.max(1, area.y + area.height - y) : rowHeight;
        const windowsInRow = (row === rows - 1 && count % 2 !== 0) ? 1 : 2;

        if (windowsInRow === 1) {
            // Last row, odd window: take full width
            zones.push({ x: area.x, y: y, width: area.width, height: h });
        } else {
            // Offset the split point to prevent four-corner intersections.
            // Even rows split at EVEN_ROW_RATIO, odd rows split at ODD_ROW_RATIO.
            const ratio = (row % 2 === 0) ? EVEN_ROW_RATIO : ODD_ROW_RATIO;
            const leftW = Math.max(1, Math.round((area.width - gap) * ratio));
            const rightW = Math.max(1, area.width - gap - leftW);

            zones.push({ x: area.x, y: y, width: leftW, height: h });
            zones.push({ x: area.x + leftW + gap, y: y, width: rightW, height: h });
        }
    }

    return zones;
}
