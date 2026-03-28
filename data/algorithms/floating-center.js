// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Floating Center
// @builtinId floating-center
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
function renderPanel(zones, startX, startY, panelW, panelH, count, gap, horizontal) {
    if (count <= 0) return;
    const totalSize = horizontal ? panelW : panelH;
    const slots = distributeEvenly(horizontal ? startX : startY, totalSize, count, gap);
    for (let i = 0; i < slots.length; i++) {
        if (horizontal) {
            zones.push({ x: slots[i].pos, y: startY, width: slots[i].size, height: panelH });
        } else {
            zones.push({ x: startX, y: slots[i].pos, width: panelW, height: slots[i].size });
        }
    }
}

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
    if (marginX <= gap || marginY <= gap) {
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

    // Top/bottom panels span the full area width
    const topH = Math.max(1, marginY - gap);
    const bottomY = area.y + marginY + centerH + gap;
    const bottomH = Math.max(1, area.y + area.height - bottomY);

    // Side panels: constrained to the center band height (between top/bottom rows)
    const sideTop = area.y + marginY;
    const sideH = centerH;

    // Side panel widths — guard against negative dimensions (m-10)
    const leftW = Math.max(1, marginX - gap);
    const rightX = area.x + marginX + centerW + gap;
    const rightW = Math.max(1, area.x + area.width - rightX);

    // Window 1: centered main window
    zones.push({
        x: centerX,
        y: centerY,
        width: centerW,
        height: centerH
    });

    // Right panel(s) — center band height only
    if (rightCount > 0) {
        if (rightW >= 1) {
            renderPanel(zones, rightX, sideTop, rightW, sideH, rightCount, gap, false);
        } else {
            for (let ri = 0; ri < rightCount; ri++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    // Left panel(s) — center band height only
    if (leftCount > 0) {
        if (leftW >= 1) {
            renderPanel(zones, area.x, sideTop, leftW, sideH, leftCount, gap, false);
        } else {
            for (let li = 0; li < leftCount; li++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    // Bottom panel(s) — span the full area width
    if (bottomCount > 0) {
        if (bottomH >= 1 && bottomY < area.y + area.height) {
            renderPanel(zones, area.x, bottomY, area.width, bottomH, bottomCount, gap, true);
        } else {
            for (let bi = 0; bi < bottomCount; bi++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    // Top panel(s) — span the full area width
    if (topCount > 0) {
        if (topH >= 1) {
            renderPanel(zones, area.x, area.y, area.width, topH, topCount, gap, true);
        } else {
            for (let ti = 0; ti < topCount; ti++) {
                zones.push({ x: centerX, y: centerY, width: centerW, height: centerH });
            }
        }
    }

    return zones;
}
