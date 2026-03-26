// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Floating Center
// @description Centered main window with peripheral panels on all sides
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.65
// @defaultMaxWindows 6
// @minimumWindows 1
// @zoneNumberDisplay all

/**
 * Floating Center layout: one centered main window with remaining windows
 * distributed as panels around all four edges (left, right, bottom, top).
 * splitRatio controls the main window size as a fraction of width and height.
 *
 * Distribution (N = remaining windows after center):
 * - 1: right panel
 * - 2: left + right panels
 * - 3: left + right + bottom panel
 * - 4: left + right + bottom + top panels
 * - 5+: one per side, extras split evenly across bottom then top
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

    var splitRatio = params.splitRatio > 0 ? params.splitRatio : 0.65;

    var centerW = Math.round(area.width * splitRatio);
    var centerH = Math.round(area.height * splitRatio);
    var marginX = Math.round((area.width - centerW) / 2);
    var marginY = Math.round((area.height - centerH) / 2);

    var zones = [];
    var remaining = count - 1;

    // Determine how many windows go on each side.
    // Assignment order: left, right, bottom, top — then extras cycle bottom, top.
    var leftCount = 0, rightCount = 0, bottomCount = 0, topCount = 0;

    if (remaining >= 1) rightCount = 1;
    if (remaining >= 2) leftCount = 1;
    if (remaining >= 3) bottomCount = 1;
    if (remaining >= 4) topCount = 1;

    // Distribute extras evenly between bottom and top
    var extras = Math.max(0, remaining - 4);
    bottomCount += Math.ceil(extras / 2);
    topCount += Math.floor(extras / 2);

    // Geometry boundaries
    var hasLeft = leftCount > 0;
    var hasRight = rightCount > 0;
    var hasTop = topCount > 0;
    var hasBottom = bottomCount > 0;

    var leftW = Math.max(1, marginX - gap);
    var rightX = area.x + marginX + centerW + gap;
    var rightW = Math.max(1, area.x + area.width - rightX);
    var topH = Math.max(1, marginY - gap);
    var bottomY = area.y + marginY + centerH + gap;
    var bottomH = Math.max(1, area.y + area.height - bottomY);

    // Side panels span the full height if no top/bottom panels exist on that edge,
    // otherwise they span only the center band (between top and bottom panels).
    var sideTop = hasTop ? (area.y + topH + gap) : area.y;
    var sideBottom = hasBottom ? (bottomY - gap) : (area.y + area.height);
    var sideH = Math.max(1, sideBottom - sideTop);

    // Window 1: centered main window
    zones.push({
        x: area.x + marginX,
        y: area.y + marginY,
        width: centerW,
        height: centerH
    });

    // Left panel(s)
    if (hasLeft) {
        var leftTileGaps = (leftCount - 1) * gap;
        var leftTileH = Math.round((sideH - leftTileGaps) / leftCount);
        for (var li = 0; li < leftCount; li++) {
            var ly = sideTop + li * (leftTileH + gap);
            var lh = (li === leftCount - 1) ? (sideTop + sideH - ly) : leftTileH;
            zones.push({ x: area.x, y: ly, width: leftW, height: lh });
        }
    }

    // Right panel(s)
    if (hasRight) {
        var rightTileGaps = (rightCount - 1) * gap;
        var rightTileH = Math.round((sideH - rightTileGaps) / rightCount);
        for (var ri = 0; ri < rightCount; ri++) {
            var ry = sideTop + ri * (rightTileH + gap);
            var rh = (ri === rightCount - 1) ? (sideTop + sideH - ry) : rightTileH;
            zones.push({ x: rightX, y: ry, width: rightW, height: rh });
        }
    }

    // Bottom panel(s) — span the center width
    if (hasBottom) {
        var btmTotalGaps = (bottomCount - 1) * gap;
        var btmTileW = Math.round((centerW - btmTotalGaps) / bottomCount);
        for (var bi = 0; bi < bottomCount; bi++) {
            var bx = area.x + marginX + bi * (btmTileW + gap);
            var bw = (bi === bottomCount - 1)
                ? (area.x + marginX + centerW - bx)
                : btmTileW;
            zones.push({ x: bx, y: bottomY, width: bw, height: bottomH });
        }
    }

    // Top panel(s) — span the center width
    if (hasTop) {
        var topTotalGaps = (topCount - 1) * gap;
        var topTileW = Math.round((centerW - topTotalGaps) / topCount);
        for (var ti = 0; ti < topCount; ti++) {
            var tx = area.x + marginX + ti * (topTileW + gap);
            var tw = (ti === topCount - 1)
                ? (area.x + marginX + centerW - tx)
                : topTileW;
            zones.push({ x: tx, y: area.y, width: tw, height: topH });
        }
    }

    return zones;
}
