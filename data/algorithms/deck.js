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

/**
 * Deck layout: one focused window with background windows peeking
 * from the right edge. splitRatio controls the peek width as a
 * fraction of screen width (default 0.05 = 5% per background window).
 *
 * Each background window extends from its peek position all the way
 * to the right edge, so they overlap each other.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    var area = params.area;

    if (count <= 0) return [];
    if (count === 1) return [area];

    // splitRatio controls the focused window width as a fraction of the screen.
    // Remaining space is divided equally among background window peek strips.
    var focusedFraction = params.splitRatio > 0 ? params.splitRatio : 0.75;
    var bgCount = count - 1;
    var focusedWidth = Math.max(1, Math.round(area.width * focusedFraction));
    var peekTotal = area.width - focusedWidth;
    var peekWidth = bgCount > 0 ? Math.max(1, Math.round(peekTotal / bgCount)) : 0;

    var zones = [];

    // First zone: the focused (front) window
    zones.push({
        x: area.x,
        y: area.y,
        width: focusedWidth,
        height: area.height
    });

    // Background windows: each starts at its peek position and
    // extends to the right edge of the area
    for (var i = 0; i < bgCount; i++) {
        var peekX = area.x + focusedWidth + (i * peekWidth);
        zones.push({
            x: peekX,
            y: area.y,
            width: area.x + area.width - peekX,
            height: area.height
        });
    }

    return zones;
}
