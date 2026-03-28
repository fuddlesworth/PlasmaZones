// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Distribute items evenly along a 1D axis with gaps between them.
 *
 * Returns an array of {pos, size} objects. The last item absorbs
 * any rounding remainder to avoid pixel gaps.
 *
 * @param {number} start - Starting position on the axis
 * @param {number} total - Total available space
 * @param {number} count - Number of items to distribute
 * @param {number} gap - Gap between items
 * @returns {Array<{pos: number, size: number}>}
 */
function distributeEvenly(start, total, count, gap) {
    if (count <= 0) return [];
    if (total <= 0) {
        const r = [];
        for (let i = 0; i < count; i++) r.push({pos: start, size: 1});
        return r;
    }
    if (count === 1) return [{pos: start, size: total}];
    const totalGaps = (count - 1) * gap;
    if (totalGaps >= total) {
        // Gaps consume all space — shrink gaps proportionally and give each item minimum size
        const shrinkRatio = (total > 0) ? total / (totalGaps + count) : 0;
        const itemSize = Math.max(1, Math.floor(shrinkRatio));
        const shrunkGap = Math.max(0, Math.floor(gap * shrinkRatio));
        const r = [];
        var pos = start;
        for (let i = 0; i < count; i++) {
            if (i === count - 1) {
                pos = Math.min(pos, start + total - 1);
                var clampedSize = Math.max(1, Math.min(itemSize, start + total - pos));
                r.push({pos: pos, size: clampedSize});
            } else {
                r.push({pos: pos, size: itemSize});
            }
            pos += itemSize + shrunkGap;
        }
        return r;
    }
    const tileSize = Math.max(1, Math.floor((total - totalGaps) / count));
    const result = [];
    for (let i = 0; i < count; i++) {
        const pos = start + i * (tileSize + gap);
        const size = (i === count - 1) ? Math.max(1, start + total - pos) : tileSize;
        result.push({pos: pos, size: size});
    }
    return result;
}
