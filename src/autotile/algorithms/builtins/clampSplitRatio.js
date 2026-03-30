// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Clamp a ratio to the global [PZ_MIN_SPLIT, PZ_MAX_SPLIT] range.
 *
 * Replaces the repeated `Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, r))`
 * pattern across algorithm scripts. Algorithms with custom clamping ranges
 * (e.g. cascade, stair) should continue to clamp explicitly.
 *
 * @param {number} ratio - The ratio to clamp
 * @returns {number} Clamped ratio in [PZ_MIN_SPLIT, PZ_MAX_SPLIT]
 */
function clampSplitRatio(ratio) {
    return Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, ratio));
}
