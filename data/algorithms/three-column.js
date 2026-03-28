// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Three Column
// @builtinId three-column
// @description Master window centered with columns on each side
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @centerLayout true
// @masterZoneIndex 0
// @supportsMemory false

/**
 * Three Column layout: center master with left/right side columns.
 * Stack windows are interleaved between left and right columns.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const gap = params.innerGap;
    const splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, params.splitRatio));
    const minSizes = params.minSizes || [];

    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    // Single window: fill area
    if (count === 1) {
        return [{ x: area.x, y: area.y, width: area.width, height: area.height }];
    }

    // Two windows: simple left/right split
    if (count === 2) {
        const contentWidth = Math.max(1, area.width - gap);
        let masterWidth = Math.floor(contentWidth * splitRatio);
        let stackWidth = contentWidth - masterWidth;

        // Joint min-width solve for 2-window case
        if (minSizes.length > 0) {
            const minMW = (0 < minSizes.length && minSizes[0].w > 0) ? minSizes[0].w : 0;
            const minSW = (1 < minSizes.length && minSizes[1].w > 0) ? minSizes[1].w : 0;
            const solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = solved.first;
            stackWidth = solved.second;
        }

        return [
            {x: area.x, y: area.y, width: masterWidth, height: area.height},
            {x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height}
        ];
    }

    // Fall back to equal columns if screen is too narrow for three columns
    if (count >= 3 && area.width < 3 * PZ_MIN_ZONE_SIZE + 2 * gap) {
        return equalColumnsLayout(area, count, gap, minSizes);
    }

    // Three or more windows: true three-column layout (masterCount = 1)
    return threeColumnLayout(area, count, gap, splitRatio, 1, minSizes);
}
