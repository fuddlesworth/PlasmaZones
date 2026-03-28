// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * When remaining space is too small for more splits, fit as many
 * equal zones as possible and stack the rest on top of the last zone.
 * Modifies `zones` array in place. Uses injected PZ_MIN_ZONE_SIZE.
 *
 * Port of TilingAlgorithm::appendGracefulDegradation.
 */
function appendGracefulDegradation(zones, remaining, leftover, innerGap) {
    if (leftover <= 0) return;
    if (remaining.width >= remaining.height) {
        var maxFit = Math.max(1, Math.floor(remaining.width / PZ_MIN_ZONE_SIZE));
        var fitCount = Math.min(leftover + 1, maxFit);
        var widths = distributeWithGaps(remaining.width, fitCount, innerGap);
        zones[zones.length - 1] = {x: remaining.x, y: remaining.y,
            width: widths[0], height: remaining.height};
        var cx = remaining.x + widths[0] + innerGap;
        for (var j = 1; j < fitCount; j++) {
            zones.push({x: cx, y: remaining.y, width: widths[j], height: remaining.height});
            cx += widths[j] + innerGap;
        }
        for (var j = fitCount; j <= leftover; j++) {
            zones.push({x: zones[zones.length-1].x, y: zones[zones.length-1].y,
                width: zones[zones.length-1].width, height: zones[zones.length-1].height});
        }
    } else {
        var maxFit = Math.max(1, Math.floor(remaining.height / PZ_MIN_ZONE_SIZE));
        var fitCount = Math.min(leftover + 1, maxFit);
        var heights = distributeWithGaps(remaining.height, fitCount, innerGap);
        zones[zones.length - 1] = {x: remaining.x, y: remaining.y,
            width: remaining.width, height: heights[0]};
        var cy = remaining.y + heights[0] + innerGap;
        for (var j = 1; j < fitCount; j++) {
            zones.push({x: remaining.x, y: cy, width: remaining.width, height: heights[j]});
            cy += heights[j] + innerGap;
        }
        for (var j = fitCount; j <= leftover; j++) {
            zones.push({x: zones[zones.length-1].x, y: zones[zones.length-1].y,
                width: zones[zones.length-1].width, height: zones[zones.length-1].height});
        }
    }
}
