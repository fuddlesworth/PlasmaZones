// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Deck
// @builtinId deck
// @description Focused window takes the left portion; remaining windows peek from the right edge. Ratio controls focused window width
// @producesOverlappingZones true
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.75
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay firstAndLast
// @supportsMemory false

/**
 * Deck layout: one focused window with background windows peeking
 * from the trailing edge. Left-to-right orientation (horizontal=false).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */

// Uses injected deckLayout(area, count, focusedFraction, horizontal)

function calculateZones(params) {
    // Overlapping layout — innerGap intentionally ignored (zones overlap by design)
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const splitRatio = clampSplitRatio(params.splitRatio);

    return deckLayout(area, count, splitRatio, false);
}
