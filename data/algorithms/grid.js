// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

var metadata = {
    name: "Grid",
    id: "grid",
    description: "Equal-sized grid layout",
    producesOverlappingZones: false,
    supportsMasterCount: false,
    supportsSplitRatio: false,
    defaultMaxWindows: 9,
    minimumWindows: 1,
    zoneNumberDisplay: "all",
    supportsMemory: false
};

/**
 * Grid layout: equal-sized NxM grid where rows and columns are
 * auto-calculated to keep cells as square as possible.
 * Last row may have fewer windows spanning the full width.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    const count = params.windowCount;
    if (count <= 0) return [];
    const area = params.area;
    if (area.width < PZ_MIN_ZONE_SIZE || area.height < PZ_MIN_ZONE_SIZE) {
        return fillArea(area, count);
    }
    const gap = params.innerGap;
    const minSizes = params.minSizes;

    // Calculate grid dimensions
    const cols = Math.ceil(Math.sqrt(count));
    const rows = Math.ceil(count / cols);

    // Build per-column minimum widths and per-row minimum heights
    const colMinWidths = [];
    const rowMinHeights = [];
    for (let c = 0; c < cols; c++) colMinWidths.push(0);
    for (let r = 0; r < rows; r++) rowMinHeights.push(0);

    if (minSizes.length > 0) {
        const fullRows = (count % cols === 0) ? rows : rows - 1;
        for (let i = 0; i < count; i++) {
            const row = Math.floor(i / cols);
            const col = i % cols;
            const mw = (i < minSizes.length && minSizes[i].w > 0) ? minSizes[i].w : 0;
            const mh = (i < minSizes.length && minSizes[i].h > 0) ? minSizes[i].h : 0;
            if (row < fullRows) {
                if (mw > colMinWidths[col]) colMinWidths[col] = mw;
                if (mh > rowMinHeights[row]) rowMinHeights[row] = mh;
            } else {
                // Last (sparse) row — only contributes to row height
                if (mh > rowMinHeights[row]) rowMinHeights[row] = mh;
            }
        }
    }

    // Calculate column widths and row heights
    const columnWidths = distributeWithOptionalMins(area.width, cols, gap, colMinWidths);
    const rowHeights = distributeWithOptionalMins(area.height, rows, gap, rowMinHeights);

    // Pre-compute column X positions
    const colX = [area.x];
    for (let c = 1; c < cols; c++) {
        colX.push(colX[c - 1] + columnWidths[c - 1] + gap);
    }

    // Pre-compute row Y positions
    const rowY = [area.y];
    for (let r = 1; r < rows; r++) {
        rowY.push(rowY[r - 1] + rowHeights[r - 1] + gap);
    }

    // Generate zones row by row
    const zones = [];
    for (let i = 0; i < count; i++) {
        const row = Math.floor(i / cols);
        const col = i % cols;

        const windowsInThisRow = (row === rows - 1) ? (count - row * cols) : cols;

        if (windowsInThisRow < cols && col === 0) {
            // Last row with fewer windows: re-distribute width
            const lastRowMinWidths = [];
            if (minSizes.length > 0) {
                for (let j = 0; j < windowsInThisRow; j++) {
                    const idx = row * cols + j;
                    lastRowMinWidths.push((idx < minSizes.length && minSizes[idx].w > 0) ? minSizes[idx].w : 0);
                }
            }
            const lastRowWidths = distributeWithOptionalMins(area.width, windowsInThisRow, gap, lastRowMinWidths);
            let lastRowX = area.x;
            for (let j = 0; j < windowsInThisRow; j++) {
                zones.push({x: lastRowX, y: rowY[row], width: lastRowWidths[j], height: rowHeights[row]});
                lastRowX += lastRowWidths[j] + gap;
            }
            break; // All remaining windows in last row handled
        }

        if (windowsInThisRow === cols) {
            zones.push({x: colX[col], y: rowY[row], width: columnWidths[col], height: rowHeights[row]});
        }
    }

    return zones;
}
