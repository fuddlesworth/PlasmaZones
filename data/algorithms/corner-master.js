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

// Guard pattern and splitRatio clamping are intentionally duplicated across
// algorithm scripts because each one runs in its own QJSEngine instance.

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

/**
 * Shared L-shape helper: master in top-left, remaining split between
 * right column and bottom row.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - total window count
 * @param {number} gap - inner gap in px
 * @param {number} splitRatio - master size fraction
 * @param {string} distribute - "alternate" or "ceil-floor"
 * @param {string} bottomWidth - "master" (only master width) or "full" (full area width)
 * @param {string} rightHeight - "full" (full area height) or "master" (only master height)
 */
// KEEP IN SYNC with quadrant-priority.js
function lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight) {
    const masterW = Math.max(1, Math.round(area.width * splitRatio - gap / 2));
    const masterH = Math.max(1, Math.round(area.height * splitRatio - gap / 2));

    const zones = [];

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
            width: Math.max(1, area.x + area.width - (area.x + masterW + gap)),
            height: area.height
        });
        return zones;
    }

    // Distribute remaining windows between right column and bottom row
    let rightCount, bottomCount;
    if (distribute === "alternate") {
        rightCount = 0;
        bottomCount = 0;
        for (let i = 1; i < count; i++) {
            if ((i - 1) % 2 === 0) rightCount++;
            else bottomCount++;
        }
    } else {
        // ceil-floor
        const remaining = count - 1;
        rightCount = Math.ceil(remaining / 2);
        bottomCount = Math.floor(remaining / 2);
    }

    // Right column
    const rightX = area.x + masterW + gap;
    const rightW = Math.max(1, area.x + area.width - rightX);
    const rightH = (rightHeight === "master" && bottomCount > 0) ? masterH : area.height;
    const rightTotalGaps = (rightCount - 1) * gap;
    const rightTileH = Math.max(1, Math.round((rightH - rightTotalGaps) / rightCount));

    for (let r = 0; r < rightCount; r++) {
        const ry = area.y + r * (rightTileH + gap);
        const rh = (r === rightCount - 1)
            ? (area.y + rightH - ry)
            : rightTileH;

        zones.push({
            x: rightX,
            y: ry,
            width: rightW,
            height: rh
        });
    }

    // Bottom row
    if (bottomCount > 0) {
        const bottomY = area.y + masterH + gap;
        const bottomH = Math.max(1, area.y + area.height - bottomY);
        const btmWidth = (bottomWidth === "full") ? area.width : masterW;
        const bottomTotalGaps = (bottomCount - 1) * gap;
        const bottomTileW = Math.max(1, Math.round((btmWidth - bottomTotalGaps) / bottomCount));

        for (let b = 0; b < bottomCount; b++) {
            const bx = area.x + b * (bottomTileW + gap);
            const bw = (b === bottomCount - 1)
                ? (area.x + btmWidth - bx)
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

function calculateZones(params) {
    const count = params.windowCount;
    const area = params.area;
    const gap = params.innerGap || 0;

    if (count <= 0) return [];
    if (count === 1) return [area];

    const splitRatio = params.splitRatio > 0 ? Math.min(params.splitRatio, 0.9) : 0.55;
    // Corner Master: alternate distribution, bottom row uses master width,
    // right column uses full height
    return lShapeLayout(area, count, gap, splitRatio, "alternate", "master", "full");
}
