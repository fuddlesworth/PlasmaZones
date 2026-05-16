// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Centered Master",
    id: "centered-master",
    description: "Master windows centered with stacks on both sides",
    producesOverlappingZones: false,
    supportsMasterCount: true,
    supportsSplitRatio: true,
    defaultSplitRatio: 0.5,
    defaultMaxWindows: 7,
    minimumWindows: 1,
    zoneNumberDisplay: "all",
    centerLayout: true,
    masterZoneIndex: 0,
    supportsMemory: false
};

/**
 * Centered Master layout.
 *
 * Master windows in center column with stacks wrapping both sides.
 * Unlike ThreeColumn (which always has exactly 1 center window),
 * CenteredMaster supports multiple masters stacked vertically in
 * the center column.
 *
 * 1 window:  full screen
 * 2 windows: master left, stack right (2-column)
 * 3+ windows: left stack, center masters, right stack (3-column)
 *   Stack windows interleave: even indices -> left, odd -> right
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];

    const area = params.area;
    const gap = params.innerGap;
    const minSizes = params.minSizes;

    // Degenerate screen — C++ wrapper also checks, belt-and-suspenders
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    const masterCount = Math.max(1, Math.min(params.masterCount || 1, count));
    const stackCount = count - masterCount;
    const splitRatio = clampSplitRatio(params.splitRatio);

    // Case 1: Only masters — delegate to shared masterStackLayout (DRY)
    if (stackCount === 0) {
        return masterStackLayout(area, count, gap, splitRatio, masterCount, minSizes, false);
    }

    // Case 2: One stack window — 2-column layout (masters left, stack right)
    if (stackCount === 1) {
        // Degenerate: gap consumes all width or height — fall back to stacking
        if (area.width - gap <= 0 || area.height < PZ_MIN_ZONE_SIZE) {
            return fillArea(area, count);
        }
        const contentWidth = area.width - gap;
        let masterWidth = Math.floor(contentWidth * splitRatio);
        let stackWidth = contentWidth - masterWidth;

        // Min-width clamping
        if (minSizes.length > 0) {
            const minMW = minSizes[0].w || 0;
            const minSW = (masterCount < minSizes.length) ? (minSizes[masterCount].w || 0) : 0;
            const solved = solveTwoPart(contentWidth, masterWidth, stackWidth, minMW, minSW);
            masterWidth = Math.max(1, solved.first);
            stackWidth = Math.max(1, solved.second);
        }

        // Masters stacked vertically on left
        const masterMinH = extractMinHeights(minSizes, masterCount);
        const mHeights = distributeWithOptionalMins(area.height, masterCount, gap, masterMinH);
        const zones = [];
        let currentY = area.y;
        for (let i = 0; i < masterCount; i++) {
            zones.push({x: area.x, y: currentY, width: masterWidth, height: mHeights[i]});
            currentY += mHeights[i] + gap;
        }

        // Single stack on right
        zones.push({x: area.x + masterWidth + gap, y: area.y, width: stackWidth, height: area.height});
        return zones;
    }

    // Case 3: 3-column layout — left stack, center masters, right stack
    return threeColumnLayout(area, count, gap, splitRatio, masterCount, minSizes);
}
