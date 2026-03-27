// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Horizontal Deck
// @description Focused window on top; remaining windows peek from the bottom edge
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
 * Horizontal Deck layout: vertical version of Deck. The focused window takes
 * the top portion, remaining windows peek from the bottom edge.
 * splitRatio controls the focused window height as a fraction of the screen.
 *
 * Uses the same deckLayout helper as deck.js but with horizontal=true.
 * Note: Each ScriptedAlgorithm runs in its own QJSEngine, so we duplicate
 * the deckLayout helper here. The function is small enough that this is
 * preferable to a complex shared-module system.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function deckLayout(area, count, focusedFraction, horizontal) {
    const axisSize = horizontal ? area.height : area.width;
    const bgCount = count - 1;
    const focusedSize = Math.max(1, Math.round(axisSize * focusedFraction));
    const peekTotal = axisSize - focusedSize;
    const peekSize = bgCount > 0 ? Math.max(1, Math.round(peekTotal / bgCount)) : 0;

    const zones = [];

    zones.push({
        x: area.x,
        y: area.y,
        width: horizontal ? area.width : focusedSize,
        height: horizontal ? focusedSize : area.height
    });

    // Overlapping layout — innerGap intentionally ignored (zones overlap by design)

    for (let i = 0; i < bgCount; i++) {
        const peekOffset = Math.min(focusedSize + i * peekSize, axisSize - 1);
        if (horizontal) {
            const peekY = area.y + peekOffset;
            zones.push({
                x: area.x,
                y: Math.min(peekY, area.y + area.height - 1),
                width: area.width,
                height: Math.max(1, area.y + area.height - peekY)
            });
        } else {
            const peekX = area.x + peekOffset;
            zones.push({
                x: Math.min(peekX, area.x + area.width - 1),
                y: area.y,
                width: Math.max(1, area.x + area.width - peekX),
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

    const focusedFraction = params.splitRatio > 0 ? Math.min(params.splitRatio, 0.9) : 0.75;
    // KEEP IN SYNC with deck.js
    return deckLayout(area, count, focusedFraction, true);
}
