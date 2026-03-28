// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Build left/right interleaving mapping for stack windows.
 * Even-indexed stacks go left, odd go right. Returns boolean array.
 *
 * @param {number} stackCount - Number of stack windows
 * @param {number} leftCount - Number of windows in left column
 * @param {number} rightCount - Number of windows in right column
 * @returns {boolean[]} true = left, false = right for each stack index
 */
function buildStackIsLeft(stackCount, leftCount, rightCount) {
    const stackIsLeft = [];
    let li = 0;
    let ri = 0;
    for (let i = 0; i < stackCount; i++) {
        if (i % 2 === 0 && li < leftCount) {
            stackIsLeft.push(true);
            li++;
        } else if (ri < rightCount) {
            stackIsLeft.push(false);
            ri++;
        } else {
            stackIsLeft.push(true);
            li++;
        }
    }
    return stackIsLeft;
}

/**
 * Compute per-column minimum widths from minSizes using stack interleaving.
 *
 * @param {Array} minSizes - Per-zone min sizes [{w,h}, ...]
 * @param {boolean[]} stackIsLeft - Interleaving map from buildStackIsLeft
 * @param {number} stackCount - Number of stack windows
 * @param {number} masterOffset - Index offset for stack windows in minSizes
 *                                (masterCount for centered-master, 1 for three-column)
 * @returns {{minLeftWidth: number, minRightWidth: number}}
 */
function interleaveMinWidths(minSizes, stackIsLeft, stackCount, masterOffset) {
    let minLeftWidth = 0;
    let minRightWidth = 0;
    for (let i = 0; i < stackCount; i++) {
        const zoneIdx = masterOffset + i;
        if (zoneIdx < minSizes.length) {
            const mw = (minSizes[zoneIdx].w > 0) ? minSizes[zoneIdx].w : 0;
            if (stackIsLeft[i]) {
                if (mw > minLeftWidth) minLeftWidth = mw;
            } else {
                if (mw > minRightWidth) minRightWidth = mw;
            }
        }
    }
    return {minLeftWidth: minLeftWidth, minRightWidth: minRightWidth};
}

/**
 * Compute per-column minimum heights from minSizes using stack interleaving.
 *
 * @param {Array} minSizes - Per-zone min sizes [{w,h}, ...]
 * @param {boolean[]} stackIsLeft - Interleaving map from buildStackIsLeft
 * @param {number} stackCount - Number of stack windows
 * @param {number} leftCount - Number of windows in left column
 * @param {number} rightCount - Number of windows in right column
 * @param {number} masterOffset - Index offset for stack windows in minSizes
 * @returns {{leftMinH: number[], rightMinH: number[]}}
 */
function interleaveMinHeights(minSizes, stackIsLeft, stackCount, leftCount, rightCount, masterOffset) {
    const leftMinH = [];
    const rightMinH = [];
    for (let i = 0; i < leftCount; i++) leftMinH.push(0);
    for (let i = 0; i < rightCount; i++) rightMinH.push(0);
    let li = 0;
    let ri = 0;
    for (let i = 0; i < stackCount; i++) {
        const zoneIdx = masterOffset + i;
        const mh = (zoneIdx < minSizes.length && minSizes[zoneIdx].h > 0) ? minSizes[zoneIdx].h : 0;
        if (stackIsLeft[i] && li < leftCount) {
            leftMinH[li] = mh;
            li++;
        } else if (!stackIsLeft[i] && ri < rightCount) {
            rightMinH[ri] = mh;
            ri++;
        }
    }
    return {leftMinH: leftMinH, rightMinH: rightMinH};
}

/**
 * Assign interleaved stack windows to left/right columns and push zones.
 *
 * @param {Array} zones - Output zones array (modified in place)
 * @param {boolean[]} stackIsLeft - Interleaving map from buildStackIsLeft
 * @param {number} stackCount - Number of stack windows
 * @param {number} leftX - X position of left column
 * @param {number} rightX - X position of right column
 * @param {number} leftWidth - Width of left column
 * @param {number} rightWidth - Width of right column
 * @param {number[]} leftHeights - Heights for left column windows
 * @param {number[]} rightHeights - Heights for right column windows
 * @param {number} areaY - Y origin
 * @param {number} gap - Inner gap
 */
function assignInterleavedStacks(zones, stackIsLeft, stackCount,
                                  leftX, rightX, leftWidth, rightWidth,
                                  leftHeights, rightHeights, areaY, gap) {
    let leftIdx = 0;
    let rightIdx = 0;
    let leftY = areaY;
    let rightY = areaY;

    for (let i = 0; i < stackCount; i++) {
        if (stackIsLeft[i]) {
            zones.push({x: leftX, y: leftY, width: leftWidth, height: leftHeights[leftIdx]});
            leftY += leftHeights[leftIdx] + gap;
            leftIdx++;
        } else {
            zones.push({x: rightX, y: rightY, width: rightWidth, height: rightHeights[rightIdx]});
            rightY += rightHeights[rightIdx] + gap;
            rightIdx++;
        }
    }
}
