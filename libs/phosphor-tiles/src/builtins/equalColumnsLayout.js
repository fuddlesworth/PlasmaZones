// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

/**
 * Equal-width columns layout with gap-aware distribution.
 * Used as a standalone layout (columns algorithm) and as a fallback
 * when other algorithms produce degenerate zones.
 *
 * @param {Object} area - {x, y, width, height}
 * @param {number} count - Number of columns
 * @param {number} gap - Inner gap between columns
 * @param {Array} minSizes - Array of {w, h} minimum size objects (may be empty)
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function equalColumnsLayout(area, count, gap, minSizes) {
    if (count <= 0) return [];
    const minWidths = extractMinWidths(minSizes, count);
    const columnWidths = distributeWithOptionalMins(area.width, count, gap, minWidths);
    const zones = [];
    let currentX = area.x;
    for (let i = 0; i < count; i++) {
        zones.push({x: currentX, y: area.y, width: columnWidths[i], height: area.height});
        currentX += columnWidths[i] + gap;
    }
    return zones;
}
