// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Extract per-element minimum widths from a minSizes array.
 * Returns empty array if minSizes is empty (caller can use this
 * to decide between distributeWithGaps vs distributeWithMinSizes).
 *
 * @param {Array} minSizes - Array of {w, h} objects
 * @param {number} count - Number of elements to extract
 * @param {number} [startIdx=0] - Starting index into minSizes
 * @returns {Array<number>} Minimum widths (empty if no minSizes)
 */
function extractMinWidths(minSizes, count, startIdx) {
    if (!minSizes || minSizes.length === 0) return [];
    startIdx = startIdx || 0;
    var result = [];
    for (var i = 0; i < count; i++) {
        var idx = startIdx + i;
        result.push((idx < minSizes.length && minSizes[idx].w > 0) ? minSizes[idx].w : 0);
    }
    return result;
}

/**
 * Extract per-element minimum heights from a minSizes array.
 * Returns empty array if minSizes is empty.
 *
 * @param {Array} minSizes - Array of {w, h} objects
 * @param {number} count - Number of elements to extract
 * @param {number} [startIdx=0] - Starting index into minSizes
 * @returns {Array<number>} Minimum heights (empty if no minSizes)
 */
function extractMinHeights(minSizes, count, startIdx) {
    if (!minSizes || minSizes.length === 0) return [];
    startIdx = startIdx || 0;
    var result = [];
    for (var i = 0; i < count; i++) {
        var idx = startIdx + i;
        result.push((idx < minSizes.length && minSizes[idx].h > 0) ? minSizes[idx].h : 0);
    }
    return result;
}
