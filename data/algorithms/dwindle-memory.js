// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Dwindle (Memory)
// @builtinId dwindle-memory
// @description Remembers split positions — resize one split without affecting others
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory true

/**
 * Dwindle layout with persistent split memory.
 *
 * When params.tree exists and has the right leaf count, zone geometries
 * are computed from the tree (preserving user-resized splits). Otherwise,
 * falls back to stateless dwindle logic.
 *
 * The prepareTilingState() is handled by the C++ ScriptedAlgorithm wrapper
 * when @supportsMemory true is set.
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

    // Degenerate screen — fall back before checking tree
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }

    // Use persistent split tree if available and leaf count matches
    // Note: minSizes are not applied when using persistent tree splits.
    // The tree stores ratios that reflect the user's manual adjustments.
    if (params.tree && params.tree.leafCount === count) {
        return applyTreeGeometry(params.tree, area, gap);
    }

    // Fallback: stateless dwindle layout (clamps splitRatio internally)
    return dwindleLayout(area, count, params.splitRatio, gap, minSizes);
}
