// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * When remaining space is too small for more splits, fit as many
 * equal zones as possible and stack the rest on top of the last zone.
 * Modifies `zones` array in place. Uses injected PZ_MIN_ZONE_SIZE.
 *
 * Port of TilingAlgorithm::appendGracefulDegradation.
 */
function appendGracefulDegradation(zones, remaining, leftover, innerGap) {
    if (zones.length === 0) return;
    if (leftover <= 0) return;

    // Helper: degrade along a single axis.
    // primaryPos/primarySize: the axis we distribute along (x/width or y/height)
    // secondaryPos/secondarySize: the cross-axis (y/height or x/width)
    // buildZone(pos, size): returns a zone rect from primary position+size
    //
    // fitCount includes 1 for re-splitting the last existing zone, so total
    // new zones produced = (fitCount - 1) fit zones + stacked remainder.
    // The stacking loop runs for (leftover - fitCount + 1) iterations,
    // yielding exactly `leftover` additional zones beyond the original.
    function degradeAxis(primarySize, primaryPos, buildZone) {
        if (primarySize <= 0) return;
        var maxFit = Math.max(1, Math.floor(primarySize / PZ_MIN_ZONE_SIZE));
        var fitCount = Math.min(leftover + 1, maxFit);
        var sizes = distributeWithGaps(primarySize, fitCount, innerGap);
        if (sizes.length === 0) return;
        zones[zones.length - 1] = buildZone(primaryPos, sizes[0]);
        var cursor = primaryPos + sizes[0] + innerGap;
        for (var j = 1; j < fitCount; j++) {
            zones.push(buildZone(cursor, sizes[j]));
            cursor += sizes[j] + innerGap;
        }
        // Stack remaining zones (that didn't fit) on top of the last zone
        for (var j2 = fitCount; j2 <= leftover; j2++) {
            var last = zones[zones.length - 1];
            zones.push({x: last.x, y: last.y, width: last.width, height: last.height});
        }
    }

    if (remaining.width >= remaining.height) {
        degradeAxis(remaining.width, remaining.x, function(pos, size) {
            return {x: pos, y: remaining.y, width: size, height: remaining.height};
        });
    } else {
        degradeAxis(remaining.height, remaining.y, function(pos, size) {
            return {x: remaining.x, y: pos, width: remaining.width, height: size};
        });
    }
}
