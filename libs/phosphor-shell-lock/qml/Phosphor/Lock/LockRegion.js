// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The largest empty region of a placement map (A3 §6 b): where the lock
// screen puts its clock, date and auth field. Pure geometry, no QML, so it
// is unit-testable and reusable by any surface that wants free space.
.pragma library

var Epsilon = 1e-6;

function _num(v) {
    var n = Number(v);
    return isNaN(n) ? 0 : n;
}

// Sorted, de-duplicated coordinate list (within Epsilon).
function _axis(values) {
    values.sort(function (a, b) {
        return a - b;
    });
    var out = [];
    for (var i = 0; i < values.length; ++i) {
        if (out.length === 0 || values[i] - out[out.length - 1] > Epsilon)
            out.push(values[i]);
    }
    return out;
}

// Index of the axis stop equal to v (v is always one of the stops).
function _stop(axis, v) {
    var lo = 0, hi = axis.length - 1;
    while (lo < hi) {
        var mid = (lo + hi) >> 1;
        if (axis[mid] < v - Epsilon)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// Largest axis-aligned rectangle of the (width x height) area that no cell
// overlaps. `cells` carry x, y, w, h in the same units as width and height
// (the caller converts placement-map fractions to pixels, or passes the
// fractions with width = height = 1). Cells outside the area are clipped;
// empty or inverted cells are ignored.
//
// Returns {x, y, width, height}, the whole area when there are no cells, or
// null when the cells cover everything.
//
// Method: compress both axes to the distinct cell edges, mark the grid
// tiles the cells cover, then run the maximal-rectangle histogram scan
// (each column's bar is the run of free tiles above the current row, in
// real units, so tile widths and heights need not be uniform). O(nx * ny)
// with nx, ny <= 2 * cells + 2.
function largestEmptyRect(cells, width, height) {
    var w = _num(width), h = _num(height);
    if (!(w > 0) || !(h > 0))
        return null;

    var rects = [];
    var xs = [0, w], ys = [0, h];
    for (var i = 0; i < (cells ? cells.length : 0); ++i) {
        var c = cells[i];
        if (!c)
            continue;
        var x1 = Math.max(0, _num(c.x)), y1 = Math.max(0, _num(c.y));
        var x2 = Math.min(w, _num(c.x) + _num(c.w)), y2 = Math.min(h, _num(c.y) + _num(c.h));
        if (x2 - x1 <= Epsilon || y2 - y1 <= Epsilon)
            continue;
        rects.push({ x1: x1, y1: y1, x2: x2, y2: y2 });
        xs.push(x1, x2);
        ys.push(y1, y2);
    }
    if (rects.length === 0)
        return { x: 0, y: 0, width: w, height: h };

    xs = _axis(xs);
    ys = _axis(ys);
    var nx = xs.length - 1, ny = ys.length - 1;

    // covered[j * nx + i] === 1 when tile (i, j) lies under a cell.
    var covered = new Array(nx * ny);
    for (var k = 0; k < covered.length; ++k)
        covered[k] = 0;
    for (var r = 0; r < rects.length; ++r) {
        var cx1 = _stop(xs, rects[r].x1), cx2 = _stop(xs, rects[r].x2);
        var cy1 = _stop(ys, rects[r].y1), cy2 = _stop(ys, rects[r].y2);
        for (var jj = cy1; jj < cy2; ++jj)
            for (var ii = cx1; ii < cx2; ++ii)
                covered[jj * nx + ii] = 1;
    }

    var bars = new Array(nx);
    for (var b = 0; b < nx; ++b)
        bars[b] = 0;
    var best = null;
    var bestArea = 0;

    for (var j = 0; j < ny; ++j) {
        var rowH = ys[j + 1] - ys[j];
        for (var i2 = 0; i2 < nx; ++i2)
            bars[i2] = covered[j * nx + i2] ? 0 : bars[i2] + rowH;

        // Largest rectangle under the histogram, bar widths from xs.
        var stack = [];
        for (var i3 = 0; i3 <= nx; ++i3) {
            var cur = i3 < nx ? bars[i3] : 0;
            while (stack.length > 0 && bars[stack[stack.length - 1]] >= cur) {
                var top = stack.pop();
                var barH = bars[top];
                if (barH <= Epsilon)
                    continue;
                var left = stack.length > 0 ? stack[stack.length - 1] + 1 : 0;
                var rw = xs[i3] - xs[left];
                var area = rw * barH;
                if (area > bestArea + Epsilon) {
                    bestArea = area;
                    best = { x: xs[left], y: ys[j + 1] - barH, width: rw, height: barH };
                }
            }
            stack.push(i3);
        }
    }
    return best;
}
