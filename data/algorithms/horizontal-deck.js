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
    var axisSize = horizontal ? area.height : area.width;
    var bgCount = count - 1;
    var focusedSize = Math.max(1, Math.round(axisSize * focusedFraction));
    var peekTotal = axisSize - focusedSize;
    var peekSize = bgCount > 0 ? Math.max(1, Math.round(peekTotal / bgCount)) : 0;

    var zones = [];

    zones.push({
        x: area.x,
        y: area.y,
        width: horizontal ? area.width : focusedSize,
        height: horizontal ? focusedSize : area.height
    });

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
    return deckLayout(area, count, focusedFraction, true);
}
