// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Shared master-stack tiling layout for both vertical and horizontal orientations.
 *
 * horizontal=false (master-stack): master on left, stack on right, both stacked vertically.
 * horizontal=true (wide): master on top, stack on bottom, both stacked horizontally.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - Window count
 * @param {number} gap - Inner gap between zones
 * @param {number} splitRatio - Master/stack split ratio
 * @param {number} masterCount - Number of master windows
 * @param {Array} minSizes - Array of {w, h} minimum size objects
 * @param {boolean} horizontal - false=left/right, true=top/bottom
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, horizontal) {
    if (count <= 0) return [];

    masterCount = Math.max(1, Math.min(masterCount, count));
    var stackCount = count - masterCount;
    splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(splitRatio, PZ_MAX_SPLIT));

    // Primary axis: the axis along which master and stack are split
    // horizontal=false: primary=width (left/right split)
    // horizontal=true: primary=height (top/bottom split)
    var primaryAxis = horizontal ? 'h' : 'w';
    var secondaryAxis = horizontal ? 'w' : 'h';
    var primarySize = horizontal ? area.height : area.width;
    var secondarySize = horizontal ? area.width : area.height;

    // Compute per-region max min for primary axis
    var minMasterPrimary = extractRegionMaxMin(minSizes, 0, masterCount, primaryAxis);
    var minStackPrimary = extractRegionMaxMin(minSizes, masterCount, count, primaryAxis);

    // Calculate master and stack primary dimensions
    var masterPrimary, stackPrimary;
    if (stackCount === 0) {
        masterPrimary = primarySize;
        stackPrimary = 0;
    } else {
        var contentPrimary = primarySize - gap;
        masterPrimary = Math.floor(contentPrimary * splitRatio);
        stackPrimary = contentPrimary - masterPrimary;
        var solved = solveTwoPart(contentPrimary, masterPrimary, stackPrimary,
                                  minMasterPrimary, minStackPrimary);
        masterPrimary = solved.first;
        stackPrimary = solved.second;
    }

    // Extract per-window min sizes for secondary axis (the stacking direction)
    var masterMinSec = horizontal
        ? extractMinWidths(minSizes, masterCount)
        : extractMinHeights(minSizes, masterCount);
    var stackMinSec = horizontal
        ? extractMinWidths(minSizes, stackCount, masterCount)
        : extractMinHeights(minSizes, stackCount, masterCount);

    // Distribute secondary dimension for master zones
    var masterSecondary = (masterMinSec.length === 0)
        ? distributeWithGaps(secondarySize, masterCount, gap)
        : distributeWithMinSizes(secondarySize, masterCount, gap, masterMinSec);

    // Generate master zones
    var zones = [];
    var primaryPos = horizontal ? area.y : area.x;
    var secondaryPos = horizontal ? area.x : area.y;
    var cursor = secondaryPos;

    for (var i = 0; i < masterCount; i++) {
        if (horizontal) {
            zones.push({x: cursor, y: primaryPos, width: masterSecondary[i], height: masterPrimary});
        } else {
            zones.push({x: primaryPos, y: cursor, width: masterPrimary, height: masterSecondary[i]});
        }
        cursor += masterSecondary[i] + gap;
    }

    // Generate stack zones
    if (stackCount > 0) {
        var stackSecondary = (stackMinSec.length === 0)
            ? distributeWithGaps(secondarySize, stackCount, gap)
            : distributeWithMinSizes(secondarySize, stackCount, gap, stackMinSec);
        var stackStart = primaryPos + masterPrimary + gap;

        cursor = secondaryPos;
        for (var i = 0; i < stackCount; i++) {
            if (horizontal) {
                zones.push({x: cursor, y: stackStart, width: stackSecondary[i], height: stackPrimary});
            } else {
                zones.push({x: stackStart, y: cursor, width: stackPrimary, height: stackSecondary[i]});
            }
            cursor += stackSecondary[i] + gap;
        }
    }

    return zones;
}
