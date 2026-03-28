// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Spiral
// @builtinId spiral
// @description Windows spiral inward from the edges
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.5
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all
// @supportsMemory false

/**
 * Spiral layout: recursively subdivides space by rotating through
 * four directions: right, down, left, up.
 *
 * Direction cycle per split:
 *   0: Right  -- split vertical,   window=left,   remaining=right
 *   1: Down   -- split horizontal, window=top,    remaining=bottom
 *   2: Left   -- split vertical,   window=right,  remaining=left
 *   3: Up     -- split horizontal, window=bottom,  remaining=top
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;
    var gap = params.innerGap || 0;
    var splitRatio = params.splitRatio;
    var minSizes = params.minSizes || [];

    // Clamp splitRatio
    splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(splitRatio, PZ_MAX_SPLIT));

    // Precompute direction-aware cumulative min dimensions
    var cumMinDims = computeCumulativeMinDims(count, minSizes, gap);
    var remainingMinW = cumMinDims.minW;
    var remainingMinH = cumMinDims.minH;

    var remaining = {x: area.x, y: area.y, width: area.width, height: area.height};
    var zones = [];

    for (var i = 0; i < count; i++) {
        // Last window or remaining area too small -- assign all of it
        if (i === count - 1 || remaining.width < PZ_MIN_ZONE_SIZE
            || remaining.height < PZ_MIN_ZONE_SIZE) {
            zones.push({x: remaining.x, y: remaining.y,
                width: remaining.width, height: remaining.height});
            appendGracefulDegradation(zones, remaining, count - i - 1, gap);
            break;
        }

        var dir = i % 4;

        if (dir === 0 || dir === 2) {
            // Vertical split
            var contentWidth = remaining.width - gap;
            if (contentWidth <= 0) {
                zones.push({x: remaining.x, y: remaining.y,
                    width: remaining.width, height: remaining.height});
                appendGracefulDegradation(zones, remaining, count - i - 1, gap);
                break;
            }
            var windowWidth = Math.floor(contentWidth * splitRatio);

            // Clamp for min sizes
            if (minSizes.length > 0 && i < minSizes.length && minSizes[i].w > 0) {
                windowWidth = Math.max(windowWidth, minSizes[i].w);
            }
            if (minSizes.length > 0 && remainingMinW[i + 1] > 0) {
                windowWidth = Math.min(windowWidth, contentWidth - remainingMinW[i + 1]);
            }
            windowWidth = Math.max(1, Math.min(windowWidth, contentWidth - 1));

            var otherWidth = contentWidth - windowWidth;

            if (dir === 0) {
                // Right: window=left, remaining=right
                zones.push({x: remaining.x, y: remaining.y,
                    width: windowWidth, height: remaining.height});
                remaining = {x: remaining.x + windowWidth + gap, y: remaining.y,
                    width: otherWidth, height: remaining.height};
            } else {
                // Left: window=right, remaining=left
                zones.push({x: remaining.x + otherWidth + gap, y: remaining.y,
                    width: windowWidth, height: remaining.height});
                remaining = {x: remaining.x, y: remaining.y,
                    width: otherWidth, height: remaining.height};
            }
        } else {
            // Horizontal split (dir === 1 or dir === 3)
            var contentHeight = remaining.height - gap;
            if (contentHeight <= 0) {
                zones.push({x: remaining.x, y: remaining.y,
                    width: remaining.width, height: remaining.height});
                appendGracefulDegradation(zones, remaining, count - i - 1, gap);
                break;
            }
            var windowHeight = Math.floor(contentHeight * splitRatio);

            // Clamp for min sizes
            if (minSizes.length > 0 && i < minSizes.length && minSizes[i].h > 0) {
                windowHeight = Math.max(windowHeight, minSizes[i].h);
            }
            if (minSizes.length > 0 && remainingMinH[i + 1] > 0) {
                windowHeight = Math.min(windowHeight, contentHeight - remainingMinH[i + 1]);
            }
            windowHeight = Math.max(1, Math.min(windowHeight, contentHeight - 1));

            var otherHeight = contentHeight - windowHeight;

            if (dir === 1) {
                // Down: window=top, remaining=bottom
                zones.push({x: remaining.x, y: remaining.y,
                    width: remaining.width, height: windowHeight});
                remaining = {x: remaining.x, y: remaining.y + windowHeight + gap,
                    width: remaining.width, height: otherHeight};
            } else {
                // Up: window=bottom, remaining=top
                zones.push({x: remaining.x, y: remaining.y + otherHeight + gap,
                    width: remaining.width, height: windowHeight});
                remaining = {x: remaining.x, y: remaining.y,
                    width: remaining.width, height: otherHeight};
            }
        }
    }

    return zones;
}
