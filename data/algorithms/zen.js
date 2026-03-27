// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Zen
// @description Centered column with margins — focused, distraction-free layout
// @producesOverlappingZones false
// @supportsMasterCount false
// @supportsSplitRatio true
// @defaultSplitRatio 0.6
// @defaultMaxWindows 4
// @minimumWindows 1
// @zoneNumberDisplay all

// Guard pattern and splitRatio clamping are intentionally duplicated across
// algorithm scripts because each one runs in its own QJSEngine instance.

/**
 * Zen layout: all windows share the same width (splitRatio of screen) and
 * stack vertically in a centered column with gaps. Screen edges are empty
 * margins for a distraction-free experience.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    const area = params.area;
    const gap = params.innerGap || 0;

    if (count <= 0) return [];

    const splitRatio = params.splitRatio > 0 ? params.splitRatio : 0.6;
    const columnWidth = Math.round(area.width * splitRatio);
    const offsetX = area.x + Math.round((area.width - columnWidth) / 2);

    if (count === 1) {
        return [{ x: offsetX, y: area.y, width: columnWidth, height: area.height }];
    }

    const totalGaps = (count - 1) * gap;
    const tileHeight = Math.round((area.height - totalGaps) / count);

    const zones = [];

    for (let i = 0; i < count; i++) {
        const y = area.y + i * (tileHeight + gap);
        const h = (i === count - 1)
            ? (area.y + area.height - y)
            : tileHeight;

        zones.push({
            x: offsetX,
            y: y,
            width: columnWidth,
            height: h
        });
    }

    return zones;
}
