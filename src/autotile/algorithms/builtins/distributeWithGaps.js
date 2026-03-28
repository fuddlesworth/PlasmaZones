// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * Distribute `total` pixels evenly across `count` slots, deducting
 * (count-1)*gap from total first. Returns array of int sizes.
 *
 * Port of TilingAlgorithm::distributeWithGaps.
 */
function distributeWithGaps(total, count, gap) {
    if (count <= 0 || total <= 0) return [];
    if (count === 1) return [total];
    var totalGaps = (count - 1) * gap;
    var available = Math.max(count, total - totalGaps);
    var base = Math.floor(available / count);
    var remainder = available % count;
    var sizes = [];
    for (var i = 0; i < count; i++) {
        var s = base;
        if (remainder > 0) { s++; remainder--; }
        sizes.push(s);
    }
    return sizes;
}
