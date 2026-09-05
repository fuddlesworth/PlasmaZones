// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

.pragma library

// .pragma library directly under the SPDX header, as in
// ConnectorGeometry.js: qt6_target_qml_sources only reads the first 128
// bytes when it decides whether the script is shared.

// Phosphor.Widgets.MiniatureEdges, the matched-edge half of the placement
// map's mode morph (A2 §2.1).
//
// A cell set is reduced to its edges: every cell contributes four lines
// (orientation, position across the map, span along it), and lines that
// coincide are merged. When the geometry changes, an old edge within
// `tolerance` of a new edge of the same orientation is RETARGETED (kept,
// and slid to the new position) rather than released and re-entered, so
// a 50/50 split present in both the old and the new geometry never
// blinks. Everything else enters or releases.
//
// Pure functions over plain objects so a test can drive them without a
// scene; PlacementMiniature keeps the persistent ListModel these diff.
//
// Edge: { o: "v" | "h", pos, start, end, occupied, key }
//   "v": a vertical line at x = pos spanning y = start..end
//   "h": a horizontal line at y = pos spanning x = start..end
//   All four numbers are fractions of the map. `occupied` is true when
//   any cell contributing the edge is occupied (edge at 80 % vs 30 %).

// Position quantum for edge keys and coincidence: 1/200 of the map, well
// under a pixel at every miniature size.
var Quantum = 0.005;
// A2 §2.1: matched when within 6 % of the map.
var DefaultTolerance = 0.06;
// A2 §2.1: 15 ms per edge in reading order, capped at 150 ms.
var StaggerMs = 15;
var StaggerCapMs = 150;

function _q(v) {
    return Math.round(v / Quantum);
}

function _num(v) {
    var n = Number(v);
    return isNaN(n) ? 0 : n;
}

// The four edges of one cell rect.
function _cellEdges(c) {
    var x = _num(c.x), y = _num(c.y), w = _num(c.w), h = _num(c.h);
    var occ = !!c.occupied;
    return [
        { o: "v", pos: x, start: y, end: y + h, occupied: occ },
        { o: "v", pos: x + w, start: y, end: y + h, occupied: occ },
        { o: "h", pos: y, start: x, end: x + w, occupied: occ },
        { o: "h", pos: y + h, start: x, end: x + w, occupied: occ }
    ];
}

// Reading order: top to bottom, then left to right, by the edge's
// top-left end.
function _readingKey(e) {
    var yy = e.o === "h" ? e.pos : e.start;
    var xx = e.o === "h" ? e.start : e.pos;
    return yy * 4 + xx;
}

function _byReading(a, b) {
    return _readingKey(a) - _readingKey(b);
}

// Reduce a cell list ({x, y, w, h, occupied} each) to merged edges in
// reading order, each with a key of orientation + quantised position +
// its index among the spans at that position.
function edgesFor(cells) {
    var raw = [];
    for (var i = 0; i < (cells ? cells.length : 0); ++i) {
        var four = _cellEdges(cells[i]);
        for (var j = 0; j < four.length; ++j)
            raw.push(four[j]);
    }
    // Group by orientation + quantised position, then merge overlapping or
    // touching spans within a group into one edge.
    var groups = {};
    var order = [];
    for (var k = 0; k < raw.length; ++k) {
        var e = raw[k];
        var g = e.o + ":" + _q(e.pos);
        if (!groups[g]) {
            groups[g] = [];
            order.push(g);
        }
        groups[g].push(e);
    }
    var out = [];
    for (var gi = 0; gi < order.length; ++gi) {
        var spans = groups[order[gi]].slice().sort(function (a, b) {
            return a.start - b.start;
        });
        var merged = [];
        for (var si = 0; si < spans.length; ++si) {
            var s = spans[si];
            var last = merged.length ? merged[merged.length - 1] : null;
            if (last && s.start <= last.end + Quantum) {
                last.end = Math.max(last.end, s.end);
                last.occupied = last.occupied || s.occupied;
                last.pos = (last.pos + s.pos) / 2;
            } else {
                merged.push({ o: s.o, pos: s.pos, start: s.start, end: s.end, occupied: s.occupied });
            }
        }
        for (var mi = 0; mi < merged.length; ++mi) {
            merged[mi].key = order[gi] + ":" + mi;
            out.push(merged[mi]);
        }
    }
    out.sort(_byReading);
    return out;
}

function _overlap(a, b) {
    return Math.max(0, Math.min(a.end, b.end) - Math.max(a.start, b.start));
}

// Diff the persistent edge set against the next geometry.
//
//   existing: [{ key, o, pos, start, end, occupied, releasing }]
//   next:     edgesFor(cells)
//
// Returns { retarget, enter, release, drop, morph }:
//   retarget: [{ key, pos, start, end, occupied }]  existing edges kept
//             and slid; a releasing edge that matches is re-adopted
//   enter:    [{ key, o, pos, start, end, occupied, delay }]  new edges,
//             in reading order with their stagger
//   release:  [key]  live edges no new edge matched
//   drop:     [key]  releasing edges of the OLDER generation, removed at
//             once so at most two generations are ever drawn (A2 §2.3);
//             only when this change is itself a morph
//   morph:    whether anything entered or released
function reconcile(existing, next, tolerance) {
    var tol = tolerance === undefined ? DefaultTolerance : tolerance;
    var used = {};
    var matched = {};
    var retarget = [];
    var enter = [];
    var i;
    for (i = 0; i < (existing ? existing.length : 0); ++i)
        used[existing[i].key] = true;

    var ordered = (next || []).slice().sort(_byReading);
    for (i = 0; i < ordered.length; ++i) {
        var n = ordered[i];
        var best = null;
        var bestScore = 0;
        for (var j = 0; j < (existing ? existing.length : 0); ++j) {
            var e = existing[j];
            if (matched[e.key] || e.o !== n.o)
                continue;
            var d = Math.abs(_num(e.pos) - n.pos);
            if (d > tol)
                continue;
            // Prefer a live edge over a releasing one, then the nearest,
            // then the one sharing the most span.
            var score = (e.releasing ? 0 : 1000) + (tol - d) * 100 + _overlap(e, n);
            if (!best || score > bestScore) {
                best = e;
                bestScore = score;
            }
        }
        if (best) {
            matched[best.key] = true;
            retarget.push({ key: best.key, pos: n.pos, start: n.start, end: n.end, occupied: n.occupied });
        } else {
            var key = n.key;
            var suffix = 0;
            while (used[key])
                key = n.key + "#" + (++suffix);
            used[key] = true;
            enter.push({
                key: key, o: n.o, pos: n.pos, start: n.start, end: n.end, occupied: n.occupied,
                delay: Math.min(StaggerCapMs, enter.length * StaggerMs)
            });
        }
    }

    var release = [];
    var stale = [];
    for (i = 0; i < (existing ? existing.length : 0); ++i) {
        var ex = existing[i];
        if (matched[ex.key])
            continue;
        if (ex.releasing)
            stale.push(ex.key);
        else
            release.push(ex.key);
    }
    var morph = enter.length > 0 || release.length > 0;
    return {
        retarget: retarget,
        enter: enter,
        release: release,
        drop: morph ? stale : [],
        morph: morph
    };
}
