// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Extract the maximum minimum dimension for a region of the minSizes array.
 * Scans minSizes[startIdx..endIdx) for the given axis ('w' or 'h') and
 * returns the largest value found.
 *
 * @param {Array} minSizes - Array of {w, h} minimum size objects
 * @param {number} startIdx - Start index (inclusive)
 * @param {number} endIdx - End index (exclusive)
 * @param {string} axis - 'w' for width, 'h' for height
 * @returns {number} Maximum minimum dimension (0 if none)
 */
function extractRegionMaxMin(minSizes, startIdx, endIdx, axis) {
    startIdx = Math.max(0, startIdx);
    let maxVal = 0;
    if (!minSizes || minSizes.length === 0) return 0;
    for (let i = startIdx; i < endIdx && i < minSizes.length; i++) {
        const v = (minSizes[i][axis] > 0) ? minSizes[i][axis] : 0;
        if (v > maxVal) maxVal = v;
    }
    return maxVal;
}
