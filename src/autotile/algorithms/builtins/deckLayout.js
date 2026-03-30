// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Deck (monocle-with-peek) tiling layout.
 *
 * The focused window takes a large fraction of the axis, and background
 * windows peek out from the remaining space, stacked behind.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - Window count
 * @param {number} focusedFraction - Fraction of axis for the focused window (0..1)
 * @param {boolean} [horizontal=false] - false=vertical peek (width axis), true=horizontal peek (height axis)
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function deckLayout(area, count, focusedFraction, horizontal = false) {
    if (count <= 0) return [];
    if (area.width <= 0 || area.height <= 0) {
        return fillArea(area, count);
    }
    const axisSize = horizontal ? area.height : area.width;
    const bgCount = count - 1;
    const focusedSize = Math.max(1, Math.floor(axisSize * focusedFraction));
    const peekTotal = axisSize - focusedSize;
    const peekSize = bgCount > 0 ? Math.max(1, Math.floor(Math.max(0, peekTotal) / bgCount)) : 0;
    const zones = [];
    zones.push({ x: area.x, y: area.y,
        width: horizontal ? area.width : focusedSize,
        height: horizontal ? focusedSize : area.height });
    for (let i = 0; i < bgCount; i++) {
        const peekOffset = Math.min(focusedSize + i * peekSize, axisSize - 1);
        if (horizontal) {
            const peekY = area.y + peekOffset;
            zones.push({ x: area.x,
                y: Math.min(peekY, area.y + area.height - 1),
                width: area.width,
                height: Math.max(1, area.y + area.height - peekY) });
        } else {
            const peekX = area.x + peekOffset;
            zones.push({
                x: Math.min(peekX, area.x + area.width - 1),
                y: area.y,
                width: Math.max(1, area.x + area.width - peekX),
                height: area.height });
        }
    }
    return zones;
}
