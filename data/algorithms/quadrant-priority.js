// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Quadrant Priority
// @description First window gets a large corner; rest fill the L-shaped remainder
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.6
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all

/**
 * Quadrant Priority layout: first window occupies a large top-left quadrant.
 * Remaining windows fill the L-shaped remainder — right column and bottom row.
 * splitRatio controls the master quadrant size (both width and height fraction).
 *
 * Distribution: right column gets ceil((n-1)/2) windows, bottom row gets floor((n-1)/2).
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

    var splitRatio = params.splitRatio > 0 ? params.splitRatio : 0.6;
    var masterW = Math.max(1, Math.round(area.width * splitRatio - gap / 2));
    var masterH = Math.max(1, Math.round(area.height * splitRatio - gap / 2));

    var zones = [];

    // Window 1: top-left master quadrant
    zones.push({
        x: area.x,
        y: area.y,
        width: masterW,
        height: masterH
    });

    if (count === 2) {
        // Single remaining window fills the entire L-shape as a right column (full height)
        zones.push({
            x: area.x + masterW + gap,
            y: area.y,
            width: area.x + area.width - (area.x + masterW + gap),
            height: area.height
        });
        return zones;
    }

    var remaining = count - 1;
    var rightCount = Math.ceil(remaining / 2);
    var bottomCount = Math.floor(remaining / 2);

    // Right column: from top to master bottom edge (or full height if no bottom row)
    var rightX = area.x + masterW + gap;
    var rightW = area.x + area.width - rightX;
    var rightH = bottomCount > 0 ? masterH : area.height;
    var rightTotalGaps = (rightCount - 1) * gap;
    var rightTileH = Math.round((rightH - rightTotalGaps) / rightCount);

    for (var i = 0; i < rightCount; i++) {
        var y = area.y + i * (rightTileH + gap);
        var h = (i === rightCount - 1)
            ? (area.y + rightH - y)
            : rightTileH;

        zones.push({
            x: rightX,
            y: y,
            width: rightW,
            height: h
        });
    }

    // Bottom row: below master, full width
    if (bottomCount > 0) {
        var bottomY = area.y + masterH + gap;
        var bottomH = area.y + area.height - bottomY;
        var bottomTotalGaps = (bottomCount - 1) * gap;
        var bottomTileW = Math.round((area.width - bottomTotalGaps) / bottomCount);

        for (var j = 0; j < bottomCount; j++) {
            var x = area.x + j * (bottomTileW + gap);
            var w = (j === bottomCount - 1)
                ? (area.x + area.width - x)
                : bottomTileW;

            zones.push({
                x: x,
                y: bottomY,
                width: w,
                height: bottomH
            });
        }
    }

    return zones;
}
