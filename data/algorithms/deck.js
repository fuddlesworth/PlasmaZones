// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Deck
// @description Focused window fills most of the screen; remaining windows peek from the right edge like a hand of cards
// @icon view-right-new
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.05
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

    var peekFraction = params.splitRatio > 0 ? params.splitRatio : 0.05;
    var peekWidth = Math.max(Math.round(area.width * peekFraction), 1);

    // Cap bgCount so the focused window keeps at least 20% of screen width
    var maxPeeks = Math.floor(area.width * 0.8 / peekWidth);
    var bgCount = Math.min(count - 1, maxPeeks > 0 ? maxPeeks : count - 1);

    // Clamp: with many background windows, peek strips consume the screen.
    // focusedWidth degrades gracefully to 1px (defaultMaxWindows=5 prevents this in practice).
    var focusedWidth = Math.max(1, area.width - bgCount * peekWidth);

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
