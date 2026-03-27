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
// @supportsMemory false

/**
 * Floating Center layout: one centered main window with remaining windows
 * distributed as panels around all four edges (left, right, bottom, top).
 * splitRatio controls the main window size as a fraction of width and height.
 *
 * The layout uses a cross-shaped arrangement:
 * - Side panels (left/right) span only the center band height
 * - Top/bottom panels span the full width of the area
 * This avoids cramped panels and ugly side-panel overflow.
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
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    const gap = params.innerGap || 0;

    const splitRatio = params.splitRatio;

    const centerW = Math.round(area.width * splitRatio);
    const centerH = Math.round(area.height * splitRatio);
    const marginX = Math.round((area.width - centerW) / 2);
    const marginY = Math.round((area.height - centerH) / 2);

    const centerX = area.x + marginX;
    const centerY = area.y + marginY;

    // Degenerate case: gaps consume margin space on either axis — panels would
    // have negative or zero dimensions.  Fall back to stacking all windows on the center.
    if (marginX < gap || marginY < gap) {
        const zones = [];
        for (let i = 0; i < count; i++) {
            zones.push({ x: area.x, y: area.y, width: area.width, height: area.height });
        }
        return zones;
    }

    const zones = [];
    const remaining = count - 1;

    // Determine how many windows go on each side.
    // Assignment order: right, left, bottom, top — then extras cycle bottom, top.
    let leftCount = 0, rightCount = 0, bottomCount = 0, topCount = 0;

    if (remaining >= 1) rightCount = 1;
    if (remaining >= 2) leftCount = 1;
    if (remaining >= 3) bottomCount = 1;
    if (remaining >= 4) topCount = 1;

    // Distribute extras evenly between bottom and top
    const extras = Math.max(0, remaining - 4);
    bottomCount += Math.ceil(extras / 2);
    topCount += Math.floor(extras / 2);

    // Geometry boundaries
    const hasTop = topCount > 0;
    const hasBottom = bottomCount > 0;

    // Top/bottom panels span the full area width
    const topH = marginY - gap;
    const bottomY = area.y + marginY + centerH + gap;
    const bottomH = area.y + area.height - bottomY;

    // Side panels: constrained to the center band height (between top/bottom rows)
    const sideTop = area.y + marginY;
    const sideH = centerH;

    // Side panel widths
    const leftW = marginX - gap;
    const rightX = area.x + marginX + centerW + gap;
    const rightW = area.x + area.width - rightX;

    // Window 1: centered main window
    zones.push({
        x: centerX,
        y: centerY,
        width: centerW,
        height: centerH
    });

    // Right panel(s) — center band height only
    // Note: rightCount is currently always 0 or 1, but the loop is kept for future-proofing.
    if (rightCount > 0) {
        if (rightW >= 1) {
            const rightTileGaps = (rightCount - 1) * gap;
            const rightTileH = Math.round((sideH - rightTileGaps) / rightCount);
            for (let ri = 0; ri < rightCount; ri++) {
                const ry = sideTop + ri * (rightTileH + gap);
                const rh = (ri === rightCount - 1) ? (sideTop + sideH - ry) : rightTileH;
                zones.push({ x: rightX, y: ry, width: rightW, height: rh });
            }
        } else {
            // Skip degenerate right panel — reassign this window to the center
            for (let ri = 0; ri < rightCount; ri++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    // Left panel(s) — center band height only
    // Note: leftCount is currently always 0 or 1, but the loop is kept for future-proofing.
    if (leftCount > 0) {
        if (leftW >= 1) {
            const leftTileGaps = (leftCount - 1) * gap;
            const leftTileH = Math.round((sideH - leftTileGaps) / leftCount);
            for (let li = 0; li < leftCount; li++) {
                const ly = sideTop + li * (leftTileH + gap);
                const lh = (li === leftCount - 1) ? (sideTop + sideH - ly) : leftTileH;
                zones.push({ x: area.x, y: ly, width: leftW, height: lh });
            }
        } else {
            // Skip degenerate left panel — reassign this window to the center
            for (let li = 0; li < leftCount; li++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    // Bottom panel(s) — span the full area width
    if (hasBottom) {
        if (bottomH >= 1 && bottomY < area.y + area.height) {
            const btmTotalGaps = (bottomCount - 1) * gap;
            const btmTileW = Math.round((area.width - btmTotalGaps) / bottomCount);
            for (let bi = 0; bi < bottomCount; bi++) {
                const bx = area.x + bi * (btmTileW + gap);
                const bw = (bi === bottomCount - 1)
                    ? (area.x + area.width - bx)
                    : btmTileW;
                zones.push({ x: bx, y: bottomY, width: bw, height: bottomH });
            }
        } else {
            for (let bi = 0; bi < bottomCount; bi++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    // Top panel(s) — span the full area width
    if (hasTop) {
        if (topH >= 1) {
            const topTotalGaps = (topCount - 1) * gap;
            const topTileW = Math.round((area.width - topTotalGaps) / topCount);
            for (let ti = 0; ti < topCount; ti++) {
                const tx = area.x + ti * (topTileW + gap);
                const tw = (ti === topCount - 1)
                    ? (area.x + area.width - tx)
                    : topTileW;
                zones.push({ x: tx, y: area.y, width: tw, height: topH });
            }
        } else {
            // Skip degenerate top panel — reassign this window to the center
            for (let ti = 0; ti < topCount; ti++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    return zones;
}
