// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Deck
// @description Focused window takes the left portion; remaining windows peek from the right edge. Ratio controls focused window width
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.75
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay firstAndLast

/**
 * Generic deck layout: one focused window with background windows peeking
 * from the trailing edge. Orientation is controlled by the `horizontal`
 * parameter (false = left-to-right, true = top-to-bottom).
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - window count
 * @param {number} focusedFraction - focused window size as fraction of axis
 * @param {boolean} horizontal - true for top-to-bottom, false for left-to-right
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function deckLayout(area, count, focusedFraction, horizontal) {
    var axisSize = horizontal ? area.height : area.width;
    var bgCount = count - 1;
    var focusedSize = Math.max(1, Math.round(axisSize * focusedFraction));
    var peekTotal = axisSize - focusedSize;
    var peekSize = bgCount > 0 ? Math.max(1, Math.round(peekTotal / bgCount)) : 0;

    var zones = [];

    // First zone: the focused (front) window
    zones.push({
        x: area.x,
        y: area.y,
        width: horizontal ? area.width : focusedSize,
        height: horizontal ? focusedSize : area.height
    });

    // Background windows: each starts at its peek position and
    // extends to the trailing edge of the area
    for (var i = 0; i < bgCount; i++) {
        var peekOffset = focusedSize + (i * peekSize);
        if (horizontal) {
            var peekY = area.y + peekOffset;
            zones.push({
                x: area.x,
                y: peekY,
                width: area.width,
                height: area.y + area.height - peekY
            });
        } else {
            var peekX = area.x + peekOffset;
            zones.push({
                x: peekX,
                y: area.y,
                width: area.x + area.width - peekX,
                height: area.height
            });
        }
    }

    return zones;
}

function calculateZones(params) {
    var count = params.windowCount;
    var area = params.area;

    if (count <= 0) return [];
    if (count === 1) return [area];

    var focusedFraction = params.splitRatio > 0 ? params.splitRatio : 0.75;
    return deckLayout(area, count, focusedFraction, false);
}
