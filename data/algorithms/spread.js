// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Spread
// @builtinId spread
// @description Windows spread evenly across the screen
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.8
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Spread layout: windows distributed evenly across the screen,
 * each sized to a fraction of its slot. Split ratio controls the
 * width/height fraction of each window within its slot.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;
    var gap = params.innerGap || 0;
    var splitRatio = params.splitRatio;
    var minSizes = params.minSizes || [];

    // Clamp widthFraction to [0.3, 1.0]
    var widthFraction = Math.max(0.3, Math.min(splitRatio, 1.0));

    // Extract per-window minimum sizes.
    // Slot minimums are scaled up by 1/widthFraction so the window minimum is
    // still met after the fraction is applied (slot * fraction >= minWidth).
    var minWidths = [];
    var slotMinWidths = [];
    var minHeights = [];
    for (var i = 0; i < count; i++) {
        var mw = (i < minSizes.length && minSizes[i].w > 0) ? minSizes[i].w : 0;
        var mh = (i < minSizes.length && minSizes[i].h > 0) ? minSizes[i].h : 0;
        minWidths.push(mw);
        slotMinWidths.push(mw > 0 ? Math.ceil(mw / widthFraction) : 0);
        minHeights.push(mh);
    }

    // Distribute slot widths respecting scaled minimum sizes
    var slotWidths = (minSizes.length === 0)
        ? distributeWithGaps(area.width, count, gap)
        : distributeWithMinSizes(area.width, count, gap, slotMinWidths);

    // splitRatio also controls height fraction
    var baseHeight = Math.max(50, Math.round(area.height * widthFraction));

    var zones = [];
    var currentX = area.x;
    for (var i = 0; i < count; i++) {
        var slotW = slotWidths[i];

        // Window width: fraction of slot, but never smaller than min width
        var winWidth = Math.max(50, Math.round(slotW * widthFraction));
        if (minWidths[i] > 0) {
            winWidth = Math.max(winWidth, minWidths[i]);
        }
        winWidth = Math.min(winWidth, slotW); // don't exceed slot

        // Window height: never smaller than min height
        var winHeight = baseHeight;
        if (minHeights[i] > 0) {
            winHeight = Math.max(winHeight, minHeights[i]);
        }
        winHeight = Math.min(winHeight, area.height); // don't exceed area

        // Center window within its slot
        var x = currentX + Math.floor((slotW - winWidth) / 2);
        var yOffset = Math.floor((area.height - winHeight) / 2);
        var y = area.y + yOffset;
        zones.push({x: x, y: y, width: winWidth, height: winHeight});
        currentX += slotW + gap;
    }

    return zones;
}
