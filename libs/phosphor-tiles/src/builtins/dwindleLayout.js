// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Stateless dwindle layout: alternating V/H splits with graceful
 * degradation. Uses injected PZ_MIN_SPLIT, PZ_MAX_SPLIT, PZ_MIN_ZONE_SIZE.
 * Returns array of zone objects.
 *
 * Port of DwindleAlgorithm::calculateZones core logic.
 */
function dwindleLayout(area, count, splitRatio, innerGap, minSizes) {
    if (count <= 0) return [];
    splitRatio = Math.min(Math.max(splitRatio, PZ_MIN_SPLIT), PZ_MAX_SPLIT);
    const cumMinDims = computeCumulativeMinDims(count, minSizes, innerGap);
    const remainingMinW = cumMinDims.minW;
    const remainingMinH = cumMinDims.minH;
    let remaining = {x: area.x, y: area.y, width: area.width, height: area.height};
    let splitVertical = true;
    const zones = [];

    // Split along a single axis, returning the window size and new remaining rect,
    // or null if the content is too small to split.
    // axis: 'w' (vertical split on width) or 'h' (horizontal split on height)
    function trySplit(axis, i) {
        const isWidth = (axis === 'w');
        const totalDim = isWidth ? remaining.width : remaining.height;
        const contentDim = totalDim - innerGap;
        if (contentDim < 2) return null;
        let windowDim = Math.floor(contentDim * splitRatio);
        const minDimArr = isWidth ? remainingMinW : remainingMinH;
        const minProp = isWidth ? 'w' : 'h';
        if (minSizes && minSizes.length > 0 && i < minSizes.length
            && minSizes[i][minProp] > 0) {
            windowDim = Math.max(windowDim, minSizes[i][minProp]);
        }
        if (minSizes && minSizes.length > 0 && minDimArr[i + 1] > 0) {
            windowDim = Math.min(windowDim, contentDim - minDimArr[i + 1]);
        }
        windowDim = Math.max(1, Math.min(windowDim, contentDim - 1));
        let zone;
        let newRemaining;
        if (isWidth) {
            zone = {x: remaining.x, y: remaining.y,
                width: windowDim, height: remaining.height};
            newRemaining = {x: remaining.x + windowDim + innerGap, y: remaining.y,
                width: contentDim - windowDim, height: remaining.height};
        } else {
            zone = {x: remaining.x, y: remaining.y,
                width: remaining.width, height: windowDim};
            newRemaining = {x: remaining.x,
                y: remaining.y + windowDim + innerGap,
                width: remaining.width, height: contentDim - windowDim};
        }
        return {zone: zone, newRemaining: newRemaining};
    }

    for (let i = 0; i < count; i++) {
        if (i === count - 1 || remaining.width < PZ_MIN_ZONE_SIZE
            || remaining.height < PZ_MIN_ZONE_SIZE
            || (splitVertical && remaining.width <= innerGap)
            || (!splitVertical && remaining.height <= innerGap)) {
            zones.push({x: remaining.x, y: remaining.y,
                width: remaining.width, height: remaining.height});
            appendGracefulDegradation(zones, remaining, count - i - 1, innerGap);
            break;
        } else {
            const result = trySplit(splitVertical ? 'w' : 'h', i);
            if (!result) {
                zones.push({x: remaining.x, y: remaining.y,
                    width: remaining.width, height: remaining.height});
                appendGracefulDegradation(zones, remaining, count - i - 1, innerGap);
                break;
            }
            zones.push(result.zone);
            remaining = result.newRemaining;
            splitVertical = !splitVertical;
        }
    }
    return zones;
}
