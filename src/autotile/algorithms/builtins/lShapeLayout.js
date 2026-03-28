// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * L-shaped tiling layout with a master zone in the top-left corner
 * and secondary zones distributed along the right and bottom edges.
 *
 * Uses injected globals PZ_MIN_SPLIT and PZ_MAX_SPLIT for ratio clamping.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - Window count
 * @param {number} gap - Inner gap between zones
 * @param {number} splitRatio - Master zone size ratio (clamped to PZ_MIN_SPLIT..PZ_MAX_SPLIT)
 * @param {string} [distribute='alternate'] - Distribution strategy: 'alternate' or 'split'
 * @param {string|number} [bottomWidth='master'] - Bottom row width: 'full', 'master', or pixel value
 * @param {string|number} [rightHeight='master'] - Right column height: 'master' or pixel value
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function lShapeLayout(area, count, gap, splitRatio, distribute = 'alternate', bottomWidth = 'master', rightHeight = 'master') {
    if (count <= 0) return [];
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }
    splitRatio = Math.max(PZ_MIN_SPLIT, Math.min(PZ_MAX_SPLIT, splitRatio));
    const masterW = Math.max(1, Math.floor(area.width * splitRatio - gap / 2));
    const masterH = Math.max(1, Math.floor(area.height * splitRatio - gap / 2));
    const zones = [{ x: area.x, y: area.y, width: masterW, height: masterH }];
    if (count <= 1) return zones;
    let rH = (rightHeight === 'master') ? masterH : area.height;
    if (typeof rightHeight === 'number') rH = rightHeight;
    if (count === 2) {
        zones.push({ x: area.x + masterW + gap, y: area.y,
            width: Math.max(1, area.x + area.width - (area.x + masterW + gap)),
            height: rH });
        return zones;
    }
    let rightCount, bottomCount;
    if (distribute === 'alternate') {
        rightCount = 0; bottomCount = 0;
        for (let i = 1; i < count; i++) {
            if ((i - 1) % 2 === 0) rightCount++; else bottomCount++;
        }
    } else {
        const remaining = count - 1;
        rightCount = Math.ceil(remaining / 2);
        bottomCount = Math.floor(remaining / 2);
    }
    const rightX = area.x + masterW + gap;
    const rightW = Math.max(1, area.x + area.width - rightX);
    if (rightHeight === 'master' && bottomCount > 0) rH = masterH;
    let btmW = (bottomWidth === 'full') ? area.width : masterW;
    if (typeof bottomWidth === 'number') btmW = bottomWidth;
    const rightTotalGaps = (rightCount - 1) * gap;
    const bottomTotalGaps = (bottomCount - 1) * gap;
    if ((rightCount > 0 && rH - rightTotalGaps <= 0) || (bottomCount > 0 && btmW - bottomTotalGaps <= 0)) {
        for (let i = 1; i < count; i++) {
            zones.push({ x: area.x, y: area.y, width: area.width, height: area.height });
        }
        return zones;
    }
    const rightTileH = (rightCount > 0) ? Math.max(1, Math.floor((rH - rightTotalGaps) / rightCount)) : 0;
    for (let r = 0; r < rightCount; r++) {
        const ry = Math.min(area.y + r * (rightTileH + gap), area.y + rH - 1);
        const rh = Math.max(1, (r === rightCount - 1) ? (area.y + rH - ry) : rightTileH);
        zones.push({ x: rightX, y: ry, width: rightW, height: rh });
    }
    if (bottomCount > 0) {
        const bottomY = area.y + masterH + gap;
        const bottomH = Math.max(1, area.y + area.height - bottomY);
        const bottomTileW = Math.max(1, Math.floor((btmW - bottomTotalGaps) / bottomCount));
        for (let b = 0; b < bottomCount; b++) {
            const bx = Math.min(area.x + b * (bottomTileW + gap), area.x + btmW - 1);
            const bw = Math.max(1, (b === bottomCount - 1) ? (area.x + btmW - bx) : bottomTileW);
            zones.push({ x: bx, y: bottomY, width: bw, height: bottomH });
        }
    }
    return zones;
}
