// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Golden Ratio
// @description Recursive splits using the golden ratio; each window gets 61.8% of the remaining space
// @icon view-split-left-right
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 6
// @minimumWindows 1

/**
 * Golden Ratio layout: recursively subdivides the screen using
 * phi (1.618...). Each split gives 61.8% to the current window
 * and 38.2% to the remaining windows. Split direction alternates
 * between vertical (left/right) and horizontal (top/bottom).
 *
 * Similar to Dwindle but with a fixed golden ratio instead of
 * a user-controlled split ratio.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    var area = params.area;
    var gap = params.innerGap || 0;

    if (count <= 0) return [];
    if (count === 1) return [area];

    // Golden ratio: the larger portion is 1/phi ~ 0.618
    var PHI_RATIO = 0.618;

    var zones = [];
    var remaining = { x: area.x, y: area.y, width: area.width, height: area.height };

    for (var i = 0; i < count; i++) {
        // Last window gets all remaining space
        if (i === count - 1) {
            zones.push(remaining);
            break;
        }

        // Early exit: remaining dimensions too small (large gaps can exhaust space)
        if (remaining.width < 1 || remaining.height < 1) {
            zones.push({x: remaining.x, y: remaining.y, width: Math.max(1, remaining.width), height: Math.max(1, remaining.height)});
            // Push remaining windows at the same position
            for (var j = i + 1; j < count; j++)
                zones.push(zones[zones.length - 1]);
            break;
        }

        // Alternate between vertical and horizontal splits
        var vertical = (i % 2 === 0);

        if (vertical) {
            var leftW = Math.round((remaining.width - gap) * PHI_RATIO);
            zones.push({ x: remaining.x, y: remaining.y, width: leftW, height: remaining.height });
            remaining = {
                x: remaining.x + leftW + gap,
                y: remaining.y,
                width: remaining.width - leftW - gap,
                height: remaining.height
            };
        } else {
            var topH = Math.round((remaining.height - gap) * PHI_RATIO);
            zones.push({ x: remaining.x, y: remaining.y, width: remaining.width, height: topH });
            remaining = {
                x: remaining.x,
                y: remaining.y + topH + gap,
                width: remaining.width,
                height: remaining.height - topH - gap
            };
        }
    }

    return zones;
}
