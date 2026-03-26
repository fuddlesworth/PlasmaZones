// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Corner Master
// @description Master window in a corner; rest fill the L-shaped remainder
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.55
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all

/**
 * Corner Master layout (see also quadrant-priority.js for a similar L-shape
 * variant with ceil/floor distribution instead of alternating).
 *
 * First window takes the top-left corner. Remaining
 * windows fill the L-shaped remainder — right column (full height) and
 * bottom row (master width).
 *
 * splitRatio controls the master window's width AND height fraction.
 *
 * Distribution for remaining windows:
 * - 2 windows: win2 fills entire right column (full height)
 * - 3 windows: win2 right column, win3 bottom row
 * - 4+: alternate between right column and bottom row
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

    var splitRatio = params.splitRatio > 0 ? params.splitRatio : 0.55;
    var masterW = Math.max(1, Math.round(area.width * splitRatio - gap / 2));
    var masterH = Math.max(1, Math.round(area.height * splitRatio - gap / 2));

    var zones = [];

    // Window 1: top-left master corner
    zones.push({
        x: area.x,
        y: area.y,
        width: masterW,
        height: masterH
    });

    if (count === 2) {
        // Window 2: right column, full height
        zones.push({
            x: area.x + masterW + gap,
            y: area.y,
            width: area.x + area.width - (area.x + masterW + gap),
            height: area.height
        });
        return zones;
    }

    // Distribute remaining windows between right column and bottom row
    // Alternating assignment: right, bottom, right, bottom, ...
    var rightWindows = [];
    var bottomWindows = [];

    for (var i = 1; i < count; i++) {
        if ((i - 1) % 2 === 0) {
            rightWindows.push(i);
        } else {
            bottomWindows.push(i);
        }
    }

    // Right column: full height, to the right of master
    var rightX = area.x + masterW + gap;
    var rightW = area.x + area.width - rightX;
    var rightCount = rightWindows.length;
    var rightTotalGaps = (rightCount - 1) * gap;
    var rightTileH = Math.round((area.height - rightTotalGaps) / rightCount);

    for (var r = 0; r < rightCount; r++) {
        var ry = area.y + r * (rightTileH + gap);
        var rh = (r === rightCount - 1)
            ? (area.y + area.height - ry)
            : rightTileH;

        zones.push({
            x: rightX,
            y: ry,
            width: rightW,
            height: rh
        });
    }

    // Bottom row: below master, master width
    if (bottomWindows.length > 0) {
        var bottomY = area.y + masterH + gap;
        var bottomH = area.y + area.height - bottomY;
        var bottomCount = bottomWindows.length;
        var bottomTotalGaps = (bottomCount - 1) * gap;
        var bottomTileW = Math.round((masterW - bottomTotalGaps) / bottomCount);

        for (var b = 0; b < bottomCount; b++) {
            var bx = area.x + b * (bottomTileW + gap);
            var bw = (b === bottomCount - 1)
                ? (area.x + masterW - bx)
                : bottomTileW;

            zones.push({
                x: bx,
                y: bottomY,
                width: bw,
                height: bottomH
            });
        }
    }

    return zones;
}
