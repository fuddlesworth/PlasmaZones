// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// @name Grid
// @builtinId grid
// @description Equal-sized grid layout
// @supportsMasterCount false
// @supportsSplitRatio false
// @defaultMaxWindows 9

/**
 * Grid layout: equal-sized NxM grid where rows and columns are
 * auto-calculated to keep cells as square as possible.
 * Last row may have fewer windows spanning the full width.
 *
 * @param {Object} params - Tiling parameters
 * @returns {Array<{x: number, y: number, width: number, height: number}>}
 */
function calculateZones(params) {
    var count = params.windowCount;
    if (count <= 0) return [];
    var area = params.area;
    var gap = params.innerGap || 0;
    var minSizes = params.minSizes || [];

    // Calculate grid dimensions
    var cols = Math.ceil(Math.sqrt(count));
    var rows = Math.ceil(count / cols);

    // Build per-column minimum widths and per-row minimum heights
    var colMinWidths = [];
    var rowMinHeights = [];
    for (var c = 0; c < cols; c++) colMinWidths.push(0);
    for (var r = 0; r < rows; r++) rowMinHeights.push(0);

    if (minSizes.length > 0) {
        var fullRows = (count % cols === 0) ? rows : rows - 1;
        for (var i = 0; i < count; i++) {
            var row = Math.floor(i / cols);
            var col = i % cols;
            var mw = (i < minSizes.length && minSizes[i].w > 0) ? minSizes[i].w : 0;
            var mh = (i < minSizes.length && minSizes[i].h > 0) ? minSizes[i].h : 0;
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
    var columnWidths = (minSizes.length === 0)
        ? distributeWithGaps(area.width, cols, gap)
        : distributeWithMinSizes(area.width, cols, gap, colMinWidths);
    var rowHeights = (minSizes.length === 0)
        ? distributeWithGaps(area.height, rows, gap)
        : distributeWithMinSizes(area.height, rows, gap, rowMinHeights);

    // Pre-compute column X positions
    var colX = [area.x];
    for (var c = 1; c < cols; c++) {
        colX.push(colX[c - 1] + columnWidths[c - 1] + gap);
    }

    // Pre-compute row Y positions
    var rowY = [area.y];
    for (var r = 1; r < rows; r++) {
        rowY.push(rowY[r - 1] + rowHeights[r - 1] + gap);
    }

    // Generate zones row by row
    var zones = [];
    for (var i = 0; i < count; i++) {
        var row = Math.floor(i / cols);
        var col = i % cols;

        var windowsInThisRow = (row === rows - 1) ? (count - row * cols) : cols;

        if (windowsInThisRow < cols && col === 0) {
            // Last row with fewer windows: re-distribute width
            var lastRowMinWidths = [];
            if (minSizes.length > 0) {
                for (var j = 0; j < windowsInThisRow; j++) {
                    var idx = row * cols + j;
                    lastRowMinWidths.push((idx < minSizes.length && minSizes[idx].w > 0) ? minSizes[idx].w : 0);
                }
            }
            var lastRowWidths = (lastRowMinWidths.length === 0)
                ? distributeWithGaps(area.width, windowsInThisRow, gap)
                : distributeWithMinSizes(area.width, windowsInThisRow, gap, lastRowMinWidths);
            var lastRowX = area.x;
            for (var j = 0; j < windowsInThisRow; j++) {
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
