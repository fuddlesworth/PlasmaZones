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

// Guard pattern and splitRatio clamping are intentionally duplicated across
// algorithm scripts because each one runs in its own QJSEngine instance.

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
    const axisSize = horizontal ? area.height : area.width;
    const bgCount = count - 1;
    const focusedSize = Math.max(1, Math.round(axisSize * focusedFraction));
    const peekTotal = axisSize - focusedSize;
    const peekSize = bgCount > 0 ? Math.max(1, Math.round(peekTotal / bgCount)) : 0;

    const zones = [];

    // First zone: the focused (front) window
    zones.push({
        x: area.x,
        y: area.y,
        width: horizontal ? area.width : focusedSize,
        height: horizontal ? focusedSize : area.height
    });

    // Overlapping layout — innerGap intentionally ignored (zones overlap by design)

    // Background windows: each starts at its peek position and
    // extends to the trailing edge of the area
    for (let i = 0; i < bgCount; i++) {
        const peekOffset = Math.min(focusedSize + i * peekSize, axisSize - 1);
        if (horizontal) {
            const peekY = area.y + peekOffset;
            zones.push({
                x: area.x,
                y: Math.min(peekY, area.y + area.height - 1),
                width: area.width,
                height: area.y + area.height - peekY
            });
        } else {
            const peekX = area.x + peekOffset;
            zones.push({
                x: Math.min(peekX, area.x + area.width - 1),
                y: area.y,
                width: area.x + area.width - peekX,
                height: area.height
            });
        }
    }

    return zones;
}

function calculateZones(params) {
    // Overlapping layout — innerGap intentionally ignored (zones overlap by design)
    const count = params.windowCount;
    const area = params.area;

    if (count <= 0) return [];
    if (count === 1) return [area];

    const focusedFraction = params.splitRatio > 0 ? params.splitRatio : 0.75;
    return deckLayout(area, count, focusedFraction, false);
}
