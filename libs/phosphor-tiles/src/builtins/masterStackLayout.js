// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

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

    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    masterCount = Math.max(1, Math.min(masterCount, count));
    const stackCount = count - masterCount;
    splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(splitRatio, PZ_MAX_SPLIT));

    // Primary axis: the axis along which master and stack are split
    // horizontal=false: primary=width (left/right split)
    // horizontal=true: primary=height (top/bottom split)
    const primaryAxis = horizontal ? 'h' : 'w';
    const secondaryAxis = horizontal ? 'w' : 'h';
    const primarySize = horizontal ? area.height : area.width;
    const secondarySize = horizontal ? area.width : area.height;

    // Compute per-region max min for primary axis
    const minMasterPrimary = extractRegionMaxMin(minSizes, 0, masterCount, primaryAxis);
    const minStackPrimary = extractRegionMaxMin(minSizes, masterCount, count, primaryAxis);

    // Calculate master and stack primary dimensions
    let masterPrimary, stackPrimary;
    if (stackCount === 0) {
        masterPrimary = primarySize;
        stackPrimary = 0;
    } else {
        const contentPrimary = primarySize - gap;
        if (contentPrimary <= 0) {
            return fillArea(area, count);
        }
        masterPrimary = Math.floor(contentPrimary * splitRatio);
        stackPrimary = contentPrimary - masterPrimary;
        const solved = solveTwoPart(contentPrimary, masterPrimary, stackPrimary,
                                    minMasterPrimary, minStackPrimary);
        masterPrimary = solved.first;
        stackPrimary = solved.second;
    }

    // Extract per-window min sizes for secondary axis (the stacking direction)
    const masterMinSec = horizontal
        ? extractMinWidths(minSizes, masterCount)
        : extractMinHeights(minSizes, masterCount);
    const stackMinSec = horizontal
        ? extractMinWidths(minSizes, stackCount, masterCount)
        : extractMinHeights(minSizes, stackCount, masterCount);

    // Degenerate secondary dimension — fill area fallback
    if (secondarySize <= 0) {
        return fillArea(area, count);
    }

    // Distribute secondary dimension for master zones
    const masterSecondary = distributeWithOptionalMins(secondarySize, masterCount, gap, masterMinSec);

    // Generate master zones
    const zones = [];
    const primaryPos = horizontal ? area.y : area.x;
    const secondaryPos = horizontal ? area.x : area.y;
    let cursor = secondaryPos;

    for (let i = 0; i < masterCount; i++) {
        if (horizontal) {
            zones.push({x: cursor, y: primaryPos, width: masterSecondary[i], height: masterPrimary});
        } else {
            zones.push({x: primaryPos, y: cursor, width: masterPrimary, height: masterSecondary[i]});
        }
        cursor += masterSecondary[i] + gap;
    }

    // Generate stack zones
    if (stackCount > 0) {
        const stackSecondary = distributeWithOptionalMins(secondarySize, stackCount, gap, stackMinSec);
        const stackStart = primaryPos + masterPrimary + gap;

        cursor = secondaryPos;
        for (let i = 0; i < stackCount; i++) {
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
