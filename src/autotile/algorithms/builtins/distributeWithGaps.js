// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Distribute `total` pixels evenly across `count` slots, deducting
 * (count-1)*gap from total first. Returns array of int sizes.
 *
 * Port of TilingAlgorithm::distributeWithGaps, with two improvements:
 *   - Proportional gap shrinking when gaps exceed available space
 *   - Single-pass overflow correction loop when Math.max inflates available
 * These edge cases may produce different results than the original C++.
 */
function distributeWithGaps(total, count, gap) {
    gap = Math.max(0, gap);
    if (count <= 0) return [];
    if (total <= 0) {
        // Return count-length array of 1s so callers can safely index
        // without producing undefined/NaN zones.
        var fallback = [];
        for (var f = 0; f < count; f++) fallback.push(1);
        return fallback;
    }
    if (count === 1) return [total];

    const totalGaps = (count - 1) * gap;
    // Ensure at least 1px per slot even when gaps dominate
    const available = Math.max(count, total - totalGaps);
    const base = Math.floor(available / count);
    let remainder = available - base * count;
    const sizes = [];
    for (let i = 0; i < count; i++) {
        let s = base;
        if (remainder > 0) { s++; remainder--; }
        sizes.push(s);
    }

    // Single-pass overflow correction: if Math.max(count, ...) inflated available
    // beyond what fits, shrink from the largest slots until sizes + gaps <= total.
    let usedSpace = totalGaps;
    for (let i = 0; i < count; i++) usedSpace += sizes[i];
    let excess = usedSpace - total;
    while (excess > 0) {
        let maxIdx = -1;
        for (let i = 0; i < count; i++) {
            if (sizes[i] > 1 && (maxIdx < 0 || sizes[i] > sizes[maxIdx])) maxIdx = i;
        }
        if (maxIdx < 0) break;
        const take = Math.min(excess, sizes[maxIdx] - 1);
        sizes[maxIdx] -= take;
        excess -= take;
    }

    return sizes;
}
