// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Apply per-window minimum size constraint from minSizes array.
 * Returns the (possibly enlarged) dimensions.
 *
 * @param {number} w - Current width
 * @param {number} h - Current height
 * @param {Array} minSizes - Array of {w, h} minimum size objects
 * @param {number} i - Window index
 * @returns {{w: number, h: number}}
 */
function applyPerWindowMinSize(w, h, minSizes, i) {
    if (minSizes && i < minSizes.length) {
        if (minSizes[i].w > 0) w = Math.max(w, minSizes[i].w);
        if (minSizes[i].h > 0) h = Math.max(h, minSizes[i].h);
    }
    return {w: w, h: h};
}
