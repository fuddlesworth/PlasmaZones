// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Distribute a total dimension among `count` items with gaps,
 * using minimum-size constraints when available.
 *
 * Wraps the repeated ternary pattern:
 *   (minDims.length === 0) ? distributeWithGaps(...) : distributeWithMinSizes(...)
 *
 * @param {number} total - Total dimension to distribute
 * @param {number} count - Number of items
 * @param {number} gap - Gap between items
 * @param {number[]} minDims - Per-item minimum dimensions (may be empty)
 * @returns {number[]} Array of distributed sizes
 */
function distributeWithOptionalMins(total, count, gap, minDims) {
    if (!minDims || minDims.length === 0) {
        return distributeWithGaps(total, count, gap);
    }
    return distributeWithMinSizes(total, count, gap, minDims);
}
