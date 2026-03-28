// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Stateless dwindle layout: alternating V/H splits with graceful
 * degradation. Uses injected PZ_MIN_SPLIT, PZ_MAX_SPLIT, PZ_MIN_ZONE_SIZE.
 * Returns array of zone objects.
 *
 * Port of DwindleAlgorithm::calculateZones core logic.
 */
function dwindleLayout(area, count, splitRatio, innerGap, minSizes) {
    splitRatio = Math.min(Math.max(splitRatio, PZ_MIN_SPLIT), PZ_MAX_SPLIT);
    const cumMinDims = computeCumulativeMinDims(count, minSizes, innerGap);
    const remainingMinW = cumMinDims.minW;
    const remainingMinH = cumMinDims.minH;
    let remaining = {x: area.x, y: area.y, width: area.width, height: area.height};
    let splitVertical = true;
    const zones = [];
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
            if (splitVertical) {
                const contentWidth = remaining.width - innerGap;
                let windowWidth = Math.floor(contentWidth * splitRatio);
                if (minSizes && minSizes.length > 0 && i < minSizes.length
                    && minSizes[i].w > 0) {
                    windowWidth = Math.max(windowWidth, minSizes[i].w);
                }
                if (minSizes && minSizes.length > 0 && remainingMinW[i + 1] > 0) {
                    windowWidth = Math.min(windowWidth, contentWidth - remainingMinW[i + 1]);
                }
                if (contentWidth < 2) {
                    // Cannot split further; give full width and degrade remaining
                    zones.push({x: remaining.x, y: remaining.y,
                        width: remaining.width, height: remaining.height});
                    appendGracefulDegradation(zones, remaining, count - i - 1, innerGap);
                    break;
                }
                windowWidth = Math.max(1, Math.min(windowWidth, contentWidth - 1));
                zones.push({x: remaining.x, y: remaining.y,
                    width: windowWidth, height: remaining.height});
                remaining = {x: remaining.x + windowWidth + innerGap, y: remaining.y,
                    width: contentWidth - windowWidth, height: remaining.height};
            } else {
                const contentHeight = remaining.height - innerGap;
                let windowHeight = Math.floor(contentHeight * splitRatio);
                if (minSizes && minSizes.length > 0 && i < minSizes.length
                    && minSizes[i].h > 0) {
                    windowHeight = Math.max(windowHeight, minSizes[i].h);
                }
                if (minSizes && minSizes.length > 0 && remainingMinH[i + 1] > 0) {
                    windowHeight = Math.min(windowHeight,
                        contentHeight - remainingMinH[i + 1]);
                }
                if (contentHeight < 2) {
                    // Cannot split further; give full height and degrade remaining
                    zones.push({x: remaining.x, y: remaining.y,
                        width: remaining.width, height: remaining.height});
                    appendGracefulDegradation(zones, remaining, count - i - 1, innerGap);
                    break;
                }
                windowHeight = Math.max(1, Math.min(windowHeight, contentHeight - 1));
                zones.push({x: remaining.x, y: remaining.y,
                    width: remaining.width, height: windowHeight});
                remaining = {x: remaining.x,
                    y: remaining.y + windowHeight + innerGap,
                    width: remaining.width, height: contentHeight - windowHeight};
            }
            splitVertical = !splitVertical;
        }
    }
    return zones;
}
