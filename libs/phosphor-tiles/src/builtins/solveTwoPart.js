// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Solve two-part min-size constraint: given contentDim split into
 * firstDim + secondDim, clamp both to their minimums.
 * Returns {first, second}.
 *
 * Port of TilingAlgorithm::solveTwoPartMinSizes.
 */
function solveTwoPart(contentDim, firstDim, secondDim, minFirst, minSecond) {
    const totalMin = Math.max(minFirst, 0) + Math.max(minSecond, 0);
    if (totalMin > contentDim && totalMin > 0) {
        firstDim = Math.floor(contentDim * Math.max(minFirst, 1) / totalMin);
        secondDim = contentDim - firstDim;
    } else {
        // Sequential two-step clamp matching C++ solveTwoPartMinSizes:
        // 1. Satisfy minFirst (may shrink secondDim)
        if (minFirst > 0 && firstDim < minFirst) {
            firstDim = minFirst;
            secondDim = contentDim - firstDim;
        }
        // 2. Satisfy minSecond (may shrink firstDim)
        if (minSecond > 0 && secondDim < minSecond) {
            secondDim = minSecond;
            firstDim = contentDim - secondDim;
        }
    }
    firstDim = Math.max(1, firstDim);
    secondDim = Math.max(1, secondDim);
    return {first: firstDim, second: secondDim};
}
