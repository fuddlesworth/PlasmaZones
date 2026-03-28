// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Internal helper: extract per-element minimum dimensions for a given axis.
 *
 * @param {Array} minSizes - Array of {w, h} objects
 * @param {number} count - Number of elements to extract
 * @param {number} startIdx - Starting index into minSizes
 * @param {string} axis - 'w' or 'h'
 * @returns {Array<number>} Minimum dimensions (empty if no minSizes)
 */
function _extractMinDims(minSizes, count, startIdx, axis) {
    if (!minSizes || minSizes.length === 0) return [];
    const result = [];
    for (let i = 0; i < count; i++) {
        const idx = startIdx + i;
        result.push((idx < minSizes.length && minSizes[idx][axis] > 0) ? minSizes[idx][axis] : 0);
    }
    return result;
}

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
function extractMinWidths(minSizes, count, startIdx = 0) {
    return _extractMinDims(minSizes, count, startIdx, 'w');
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
function extractMinHeights(minSizes, count, startIdx = 0) {
    return _extractMinDims(minSizes, count, startIdx, 'h');
}
