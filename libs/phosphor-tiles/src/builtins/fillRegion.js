// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Create an array of identical zones, all covering the same region.
 * Used as a degenerate-gap fallback when gaps exceed available space.
 * Unlike fillArea(), this takes explicit x/y/w/h for partial regions.
 *
 * @param {number} x - Zone x coordinate
 * @param {number} y - Zone y coordinate
 * @param {number} w - Zone width
 * @param {number} h - Zone height
 * @param {number} count - Number of zones to create
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function fillRegion(x, y, w, h, count) {
    const zones = [];
    for (let i = 0; i < count; i++) {
        zones.push({x: x, y: y, width: w, height: h});
    }
    return zones;
}
