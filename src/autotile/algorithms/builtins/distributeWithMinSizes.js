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
    if (count <= 0 || total <= 0) return [];
    if (count === 1) return [total];
    var totalGaps = (count - 1) * gap;
    var available = Math.max(count, total - totalGaps);
    if (!minDims || minDims.length === 0) {
        return distributeWithGaps(total, count, gap);
    }
    var mins = []; var totalMin = 0;
    for (var i = 0; i < count; i++) {
        var m = (i < minDims.length && minDims[i] > 0) ? minDims[i] : 1;
        mins.push(m); totalMin += m;
    }
    var sizes = new Array(count);
    if (totalMin >= available) {
        var remaining = available; var remainingMin = totalMin;
        for (var i = 0; i < count; i++) {
            var allocated;
            if (remainingMin > 0) {
                allocated = Math.floor(remaining * mins[i] / remainingMin);
            } else {
                allocated = Math.floor(remaining / Math.max(1, count - i));
            }
            allocated = Math.max(1, Math.min(allocated, remaining));
            sizes[i] = allocated;
            remaining -= allocated; remainingMin -= mins[i];
        }
        // Ensure sizes don't exceed available (rounding can cause overshoot)
        var totalAllocated = 0;
        for (var j = 0; j < count; j++) totalAllocated += sizes[j];
        if (totalAllocated > available && count > 0) {
            sizes[count - 1] = Math.max(1, sizes[count - 1] - (totalAllocated - available));
        }
    } else {
        var base = Math.floor(available / count);
        var rem = available % count;
        var equalSizes = [];
        for (var i = 0; i < count; i++) {
            var s = base; if (rem > 0) { s++; rem--; } equalSizes.push(s);
        }
        var equalSatisfies = true;
        for (var i = 0; i < count; i++) {
            if (equalSizes[i] < mins[i]) { equalSatisfies = false; break; }
        }
        if (equalSatisfies) {
            sizes = equalSizes;
        } else {
            var equalShare = Math.floor(available / count);
            var surplus = available - totalMin;
            var unconstrainedCount = 0;
            for (var i = 0; i < count; i++) {
                sizes[i] = mins[i];
                if (mins[i] <= equalShare) unconstrainedCount++;
            }
            if (unconstrainedCount > 0 && surplus > 0) {
                var sBase = Math.floor(surplus / unconstrainedCount);
                var sRem = surplus % unconstrainedCount;
                for (var i = 0; i < count; i++) {
                    if (mins[i] <= equalShare) {
                        sizes[i] += sBase;
                        if (sRem > 0) { sizes[i]++; sRem--; }
                    }
                }
            } else if (surplus > 0) {
                var sBase = Math.floor(surplus / count);
                var sRem = surplus % count;
                for (var i = 0; i < count; i++) {
                    sizes[i] += sBase;
                    if (sRem > 0) { sizes[i]++; sRem--; }
                }
            }
        }
    }
    return sizes;
}
