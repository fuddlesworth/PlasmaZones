// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Distribute `total` pixels across `count` slots respecting per-slot
 * minimum dimensions. Uses proportional fallback when unsatisfiable,
 * unconstrained-surplus optimization otherwise. Returns array of int sizes.
 *
 * Port of TilingAlgorithm::distributeWithMinSizes.
 */
function distributeWithMinSizes(total, count, gap, minDims) {
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
    const available = Math.max(count, total - totalGaps);
    if (!minDims || minDims.length === 0) {
        return distributeWithGaps(total, count, gap);
    }
    const mins = [];
    let totalMin = 0;
    for (let i = 0; i < count; i++) {
        const m = (i < minDims.length && minDims[i] > 0) ? minDims[i] : 1;
        mins.push(m);
        totalMin += m;
    }
    let sizes = new Array(count);
    if (totalMin >= available) {
        let remaining = available;
        let remainingMin = totalMin;
        for (let i = 0; i < count; i++) {
            let allocated;
            if (remainingMin > 0) {
                allocated = Math.floor(remaining * mins[i] / remainingMin);
            } else {
                allocated = Math.floor(remaining / Math.max(1, count - i));
            }
            allocated = Math.max(1, Math.min(allocated, remaining));
            sizes[i] = allocated;
            remaining -= allocated;
            remainingMin -= mins[i];
        }
        // Correct rounding errors: distribute across elements, not just last
        let currentSum = 0;
        for (let i = 0; i < count; i++) currentSum += sizes[i];
        let diff = currentSum - available;
        if (diff > 0) {
            // Overshoot: subtract from trailing elements first (clamped to >= 1)
            for (let pass = 0; diff > 0 && pass < count; pass++) {
                for (let i = count - 1; i >= 0 && diff > 0; i--) {
                    if (sizes[i] > 1) {
                        sizes[i]--;
                        diff--;
                    }
                }
            }
        } else if (diff < 0) {
            // Undershoot: add to smallest elements first
            for (let pass = 0; diff < 0 && pass < count; pass++) {
                for (let i = 0; i < count && diff < 0; i++) {
                    sizes[i]++;
                    diff++;
                }
            }
        }
    } else {
        const base = Math.floor(available / count);
        let rem = available % count;
        const equalSizes = [];
        for (let i = 0; i < count; i++) {
            let s = base;
            if (rem > 0) { s++; rem--; }
            equalSizes.push(s);
        }
        let equalSatisfies = true;
        for (let i = 0; i < count; i++) {
            if (equalSizes[i] < mins[i]) { equalSatisfies = false; break; }
        }
        if (equalSatisfies) {
            sizes = equalSizes;
        } else {
            const equalShare = Math.floor(available / count);
            const surplus = available - totalMin;
            let unconstrainedCount = 0;
            for (let i = 0; i < count; i++) {
                sizes[i] = mins[i];
                if (mins[i] <= equalShare) unconstrainedCount++;
            }
            if (unconstrainedCount > 0 && surplus > 0) {
                const sBase = Math.floor(surplus / unconstrainedCount);
                let sRem = surplus % unconstrainedCount;
                for (let i = 0; i < count; i++) {
                    if (mins[i] <= equalShare) {
                        sizes[i] += sBase;
                        if (sRem > 0) { sizes[i]++; sRem--; }
                    }
                }
            } else if (surplus > 0) {
                const sBase = Math.floor(surplus / count);
                let sRem = surplus % count;
                for (let i = 0; i < count; i++) {
                    sizes[i] += sBase;
                    if (sRem > 0) { sizes[i]++; sRem--; }
                }
            }
        }
    }
    return sizes;
}
