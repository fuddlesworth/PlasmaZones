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

// Guard pattern and splitRatio clamping are intentionally duplicated across
// algorithm scripts because each one runs in its own QJSEngine instance.

/**
 * Quadrant Priority layout: first window occupies a large top-left quadrant.
 * Remaining windows fill the L-shaped remainder — right column and bottom row.
 * splitRatio controls the master quadrant size (both width and height fraction).
 *
 * Distribution: right column gets ceil((n-1)/2) windows, bottom row gets floor((n-1)/2).
 * Right column height constrained to master height when bottom row exists.
 * Bottom row spans the full area width.
 *
 * Uses the same lShapeLayout helper as corner-master.js. Each script runs in
 * its own QJSEngine, so the helper is duplicated (small enough to be acceptable).
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */

function lShapeLayout(area, count, gap, splitRatio, distribute, bottomWidth, rightHeight) {
    const masterW = Math.max(1, Math.round(area.width * splitRatio - gap / 2));
    const masterH = Math.max(1, Math.round(area.height * splitRatio - gap / 2));

    const zones = [];

    zones.push({
        x: area.x,
        y: area.y,
        width: masterW,
        height: masterH
    });

    if (count === 2) {
        zones.push({
            x: area.x + masterW + gap,
            y: area.y,
            width: Math.max(1, area.x + area.width - (area.x + masterW + gap)),
            height: area.height
        });
        return zones;
    }

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

    const splitRatio = params.splitRatio > 0 ? Math.min(params.splitRatio, 0.9) : 0.6;
    // Quadrant Priority: ceil/floor distribution, bottom row spans full width,
    // right column constrained to master height
    return lShapeLayout(area, count, gap, splitRatio, "ceil-floor", "full", "master");
}
