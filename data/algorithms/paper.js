// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Paper
// @description Equal-width overlapping pages like a document viewer; splitRatio controls page width
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.8
// @defaultMaxWindows 6
// @minimumWindows 1
// @zoneNumberDisplay last

/**
 * Paper layout: each window is an equal-width "page" (default 80%
 * of screen width) distributed evenly across the screen. Windows
 * overlap like pages in a document viewer.
 *
 * splitRatio controls the page width as a fraction of the screen
 * width (0.8 = each page is 80% of screen width).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    var area = params.area;

    if (count <= 0) return [];
    if (count === 1) return [area];

    var pageRatio = params.splitRatio > 0 ? params.splitRatio : 0.8;
    var pageWidth = Math.round(area.width * pageRatio);
    if (pageWidth < 1) pageWidth = 1;
    if (pageWidth > area.width) pageWidth = area.width;

    // Distribute pages evenly across the remaining space.
    // The leftover space is shared as offsets between pages.
    var leftover = area.width - pageWidth;
    // When pageWidth >= area.width, all pages stack at the same position (degenerate case)
    var step = (count > 1) ? Math.round(leftover / (count - 1)) : 0;

    var zones = [];
    for (var i = 0; i < count; i++) {
        zones.push({
            x: area.x + (i * step),
            y: area.y,
            width: pageWidth,
            height: area.height
        });
    }

    return zones;
}
