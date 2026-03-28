// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Distribute `total` pixels evenly across `count` slots, deducting
 * (count-1)*gap from total first. Returns array of int sizes.
 *
 * Port of TilingAlgorithm::distributeWithGaps.
 */
function distributeWithGaps(total, count, gap) {
    if (count <= 0) return [];
    if (total <= 0) return [];
    if (count === 1) return [total];
    const totalGaps = (count - 1) * gap;
    const available = Math.max(count, total - totalGaps);
    const base = Math.floor(available / count);
    let remainder = available % count;
    const sizes = [];
    for (let i = 0; i < count; i++) {
        let s = base;
        if (remainder > 0) { s++; remainder--; }
        sizes.push(s);
    }
    // Ensure total sizes + gaps don't exceed the original total
    let usedSpace = totalGaps;
    for (let i = 0; i < count; i++) usedSpace += sizes[i];
    if (usedSpace > total) {
        // Proportionally shrink sizes to fit
        const shrinkRatio = Math.max(0, total - totalGaps) / Math.max(1, usedSpace - totalGaps);
        for (let i = 0; i < count; i++) {
            sizes[i] = Math.max(1, Math.floor(sizes[i] * shrinkRatio));
        }
        // After shrinking, Math.max(1, ...) floors may cause sum to exceed total.
        // Distribute excess across elements from largest to smallest (clamped to 1).
        let shrunkSum = 0;
        for (let i = 0; i < count; i++) shrunkSum += sizes[i];
        let excess = shrunkSum + totalGaps - total;
        while (excess > 0) {
            let maxIdx = -1;
            for (let i = 0; i < count; i++) {
                if (sizes[i] > 1 && (maxIdx < 0 || sizes[i] > sizes[maxIdx])) maxIdx = i;
            }
            if (maxIdx < 0) break; // All at minimum
            const take = Math.min(excess, sizes[maxIdx] - 1);
            sizes[maxIdx] -= take;
            excess -= take;
        }
    }
    // Final boundary clamping: sizes + gaps must never exceed total
    let finalSum = totalGaps;
    for (let i = 0; i < count; i++) finalSum += sizes[i];
    if (finalSum > total) {
        const sizeOnly = Math.max(0, total - totalGaps);
        let currentSizeSum = 0;
        for (let i = 0; i < count; i++) currentSizeSum += sizes[i];
        if (currentSizeSum > 0) {
            for (let i = 0; i < count; i++) {
                sizes[i] = Math.max(1, Math.floor(sizes[i] * sizeOnly / currentSizeSum));
            }
        }
        // Correct any remaining overflow from Math.max(1, ...) floors
        let correctedSum = 0;
        for (let i = 0; i < count; i++) correctedSum += sizes[i];
        let overshoot = correctedSum + totalGaps - total;
        while (overshoot > 0) {
            let maxIdx = -1;
            for (let i = 0; i < count; i++) {
                if (sizes[i] > 1 && (maxIdx < 0 || sizes[i] > sizes[maxIdx])) maxIdx = i;
            }
            if (maxIdx < 0) break;
            const take2 = Math.min(overshoot, sizes[maxIdx] - 1);
            sizes[maxIdx] -= take2;
            overshoot -= take2;
        }
    }
    return sizes;
}
