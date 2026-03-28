// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Create an array of identical zones, all covering the full area.
 * Used as a degenerate-screen fallback when there is insufficient
 * space for proper tiling.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - Number of zones to create
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function fillArea(area, count) {
    var zones = [];
    for (var i = 0; i < count; i++) {
        zones.push({x: area.x, y: area.y, width: area.width, height: area.height});
    }
    return zones;
}
