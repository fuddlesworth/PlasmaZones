// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Floating Center
// @description Centered main window with peripheral panels for reference windows
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.65
// @defaultMaxWindows 5
// @minimumWindows 1
// @zoneNumberDisplay all

/**
 * Floating Center layout: one centered main window with remaining windows
 * distributed as panels around the edges (left, right, bottom, top).
 * splitRatio controls the main window size as a fraction of width and height.
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

    // Window 1: centered main window
    zones.push({
        x: area.x + marginX,
        y: area.y + marginY,
        width: centerW,
        height: centerH
    });

    if (count === 2) {
        // Window 2: right panel, full height
        zones.push({
            x: area.x + marginX + centerW + gap,
            y: area.y,
            width: area.x + area.width - (area.x + marginX + centerW + gap),
            height: area.height
        });
        return zones;
    }

    if (count === 3) {
        // Window 2: left panel, full height
        zones.push({
            x: area.x,
            y: area.y,
            width: Math.max(1, marginX - gap),
            height: area.height
        });
        // Window 3: right panel, full height
        zones.push({
            x: area.x + marginX + centerW + gap,
            y: area.y,
            width: area.x + area.width - (area.x + marginX + centerW + gap),
            height: area.height
        });
        return zones;
    }

    // 4+ windows: left, right, then bottom row for the rest
    var leftPanelW = Math.max(1, marginX - gap);
    var rightPanelX = area.x + marginX + centerW + gap;
    var rightPanelW = area.x + area.width - rightPanelX;
    var bottomPanelY = area.y + marginY + centerH + gap;
    var bottomPanelH = area.y + area.height - bottomPanelY;

    // Window 2: left panel (top portion, above bottom row)
    zones.push({
        x: area.x,
        y: area.y,
        width: leftPanelW,
        height: area.height
    });

    // Window 3: right panel (top portion, above bottom row)
    zones.push({
        x: rightPanelX,
        y: area.y,
        width: rightPanelW,
        height: area.height
    });

    // Windows 4+: bottom row spanning center width
    var bottomCount = count - 3;
    var bottomTotalGaps = (bottomCount - 1) * gap;
    var bottomTileW = Math.round((centerW - bottomTotalGaps) / bottomCount);

    for (var i = 0; i < bottomCount; i++) {
        var x = area.x + marginX + i * (bottomTileW + gap);
        var w = (i === bottomCount - 1)
            ? (area.x + marginX + centerW - x)
            : bottomTileW;

        zones.push({
            x: x,
            y: bottomPanelY,
            width: w,
            height: bottomPanelH
        });
    }

    return zones;
}
