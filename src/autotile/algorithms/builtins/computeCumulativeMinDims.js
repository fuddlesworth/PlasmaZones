// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Compute direction-aware cumulative min dimensions for alternating
 * V/H split layouts (Dwindle, Spiral). Returns {minW: [], minH: []}.
 *
 * Port of TilingAlgorithm::computeAlternatingCumulativeMinDims.
 */
function computeCumulativeMinDims(windowCount, minSizes, innerGap) {
    const minW = new Array(windowCount + 1);
    const minH = new Array(windowCount + 1);
    for (let i = 0; i <= windowCount; i++) { minW[i] = 0; minH[i] = 0; }
    if (!minSizes || minSizes.length === 0) return {minW: minW, minH: minH};
    for (let i = windowCount - 1; i >= 0; i--) {
        const mw = (i < minSizes.length && minSizes[i].w > 0) ? minSizes[i].w : 0;
        const mh = (i < minSizes.length && minSizes[i].h > 0) ? minSizes[i].h : 0;
        const splitV = (i % 2 === 0);
        if (splitV) {
            minW[i] = mw + ((i < windowCount - 1 && minW[i+1] > 0) ? innerGap + minW[i+1] : 0);
            minH[i] = Math.max(mh, minH[i+1]);
        } else {
            minH[i] = mh + ((i < windowCount - 1 && minH[i+1] > 0) ? innerGap + minH[i+1] : 0);
            minW[i] = Math.max(mw, minW[i+1]);
        }
    }
    return {minW: minW, minH: minH};
}
