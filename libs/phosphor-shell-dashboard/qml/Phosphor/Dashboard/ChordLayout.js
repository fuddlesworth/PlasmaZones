// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

.pragma library

// .pragma library directly under the SPDX header, as in the widgets'
// scripts: qt6_target_qml_sources only reads the first 128 bytes when it
// decides whether the script is shared.

// Phosphor.Dashboard.ChordLayout, the cheatsheet's placement rule (A3
// §10): where each chord label sits on the placement map.
//
// Pure functions over plain objects, so a test drives them without a
// scene. The sheet feeds `place()` the screen's cells in SCREEN PIXELS
// ({id, x, y, w, h, zoneNumber, focused}), the focused cell's id, the
// catalog rows ({id, label, description, triggers, assigned, mode}) and
// the screen's mode, and gets back the labels to draw:
//
//   spatial: [{ id, label, chord, assigned, x, y, description }]
//            anchored at (x, y) = the label's CENTRE, on the rect or gap
//            the chord acts on
//   column:  [{ id, label, chord, assigned, description }]
//            the non-spatial chords, in catalog order
//
// Placement, by action id:
//   move_window_*, swap_window_*, span_window_*   the focused cell's edge
//         in that direction (move on the edge, swap and span stacked
//         under it)
//   focus_zone_*, scroll_focus_column_left/right  the gap between the
//         focused cell and its neighbour in that direction; the edge
//         itself, nudged outward, when there is no neighbour
//   snap_to_zone_N   centred on the cell numbered N (its `zoneNumber`),
//         or the N-th cell in reading order when the mode numbers none
//   toggle_window_float and the focused-column verbs   centred on the
//         focused cell, stacked
//   everything else   the column
// Labels sharing an anchor stack downward by `StackStep`. A chord whose
// `mode` does not apply to the screen's mode is dropped (A3 §10 e).
// Unbound chords keep their place, drawn dimmed by the sheet.

var StackStep = 24;
// How far outside an edge a gap label sits when the cell has no
// neighbour that way.
var EdgeNudge = 28;

var Mode = { none: -1, snapping: 0, tiling: 1, scrolling: 2 };

// Which catalog `mode` tags apply on a screen in `mode`.
function modeApplies(rowMode, mode) {
    switch (rowMode) {
    case "all":
        return true;
    case "snapping":
        return mode === Mode.snapping;
    case "autotile":
        return mode === Mode.tiling;
    case "scrolling":
        return mode === Mode.scrolling;
    case "managed":
        return mode === Mode.tiling || mode === Mode.scrolling;
    case "layouts":
        // A capability tag: every engine provides something to pick from
        // (layouts, algorithms, templates).
        return mode !== Mode.none;
    default:
        return true;
    }
}

// The chord as keys, for the label: "Meta+Left" reads "Meta ←".
function chordText(trigger) {
    if (!trigger)
        return "";
    var arrows = { "Left": "←", "Right": "→", "Up": "↑", "Down": "↓" };
    var parts = String(trigger).split("+");
    var out = [];
    for (var i = 0; i < parts.length; ++i) {
        var p = parts[i];
        if (p === "" && i < parts.length - 1) {
            // "Meta++" is the plus key.
            out.push("+");
            ++i;
            continue;
        }
        out.push(arrows[p] !== undefined ? arrows[p] : p);
    }
    return out.join(" ");
}

function _num(v) {
    var n = Number(v);
    return isNaN(n) ? 0 : n;
}

function _direction(id, prefix) {
    if (id.indexOf(prefix) !== 0)
        return "";
    var d = id.substring(prefix.length);
    return d === "left" || d === "right" || d === "up" || d === "down" ? d : "";
}

var EdgeVerbs = ["move_window_", "swap_window_", "span_window_"];
var GapVerbs = ["focus_zone_", "scroll_focus_column_"];
var CentreVerbs = {
    "toggle_window_float": true,
    "restore_window_size": true,
    "push_to_empty_zone": true,
    "scroll_maximize_column": true,
    "scroll_toggle_windowed_fullscreen": true,
    "scroll_maximize_to_edges": true,
    "scroll_expand_column": true,
    "scroll_toggle_column_tabbed": true,
    "scroll_center_column": true,
    "scroll_move_to_floating": true,
    "scroll_move_to_tiling": true
};
var ZonePrefix = "snap_to_zone_";

function _edgePoint(c, dir) {
    switch (dir) {
    case "left":
        return { x: c.x, y: c.y + c.h / 2 };
    case "right":
        return { x: c.x + c.w, y: c.y + c.h / 2 };
    case "up":
        return { x: c.x + c.w / 2, y: c.y };
    default:
        return { x: c.x + c.w / 2, y: c.y + c.h };
    }
}

function _overlap(a0, a1, b0, b1) {
    return Math.max(0, Math.min(a1, b1) - Math.max(a0, b0));
}

// The nearest cell beyond `dir` of `c` that shares its span on the other
// axis, or null.
function neighbour(cells, c, dir) {
    var best = null;
    var bestGap = Infinity;
    for (var i = 0; i < cells.length; ++i) {
        var o = cells[i];
        if (o.id === c.id)
            continue;
        var gap;
        if (dir === "left" || dir === "right") {
            if (_overlap(c.y, c.y + c.h, o.y, o.y + o.h) <= 0)
                continue;
            gap = dir === "left" ? c.x - (o.x + o.w) : o.x - (c.x + c.w);
        } else {
            if (_overlap(c.x, c.x + c.w, o.x, o.x + o.w) <= 0)
                continue;
            gap = dir === "up" ? c.y - (o.y + o.h) : o.y - (c.y + c.h);
        }
        // The gap may be slightly negative for touching cells.
        if (gap < -1)
            continue;
        if (gap < bestGap) {
            bestGap = gap;
            best = o;
        }
    }
    return best;
}

function _gapPoint(cells, c, dir) {
    var n = neighbour(cells, c, dir);
    var e = _edgePoint(c, dir);
    if (!n) {
        switch (dir) {
        case "left":
            return { x: e.x - EdgeNudge, y: e.y };
        case "right":
            return { x: e.x + EdgeNudge, y: e.y };
        case "up":
            return { x: e.x, y: e.y - EdgeNudge };
        default:
            return { x: e.x, y: e.y + EdgeNudge };
        }
    }
    switch (dir) {
    case "left":
        return { x: (n.x + n.w + c.x) / 2, y: e.y };
    case "right":
        return { x: (c.x + c.w + n.x) / 2, y: e.y };
    case "up":
        return { x: e.x, y: (n.y + n.h + c.y) / 2 };
    default:
        return { x: e.x, y: (c.y + c.h + n.y) / 2 };
    }
}

function _byReading(a, b) {
    return a.y !== b.y ? a.y - b.y : a.x - b.x;
}

// The cell addressed by digit `n` (1-based): the one numbered n, else
// the n-th in reading order when nothing is numbered.
function zoneCell(cells, n) {
    var numbered = false;
    for (var i = 0; i < cells.length; ++i) {
        if (_num(cells[i].zoneNumber) > 0) {
            numbered = true;
            if (_num(cells[i].zoneNumber) === n)
                return cells[i];
        }
    }
    if (numbered)
        return null;
    var ordered = cells.slice().sort(_byReading);
    return n >= 1 && n <= ordered.length ? ordered[n - 1] : null;
}

function _cellById(cells, id) {
    for (var i = 0; i < cells.length; ++i) {
        if (cells[i].id === id)
            return cells[i];
    }
    return null;
}

function _norm(cells) {
    var out = [];
    for (var i = 0; i < (cells ? cells.length : 0); ++i) {
        var c = cells[i];
        out.push({
            id: String(c.id), x: _num(c.x), y: _num(c.y), w: _num(c.w), h: _num(c.h),
            zoneNumber: _num(c.zoneNumber), focused: !!c.focused
        });
    }
    return out;
}

function place(cellsIn, focusedId, rows, mode) {
    var cells = _norm(cellsIn);
    var focused = _cellById(cells, focusedId);
    if (!focused) {
        for (var f = 0; f < cells.length; ++f) {
            if (cells[f].focused) {
                focused = cells[f];
                break;
            }
        }
    }
    var spatial = [];
    var column = [];
    var stacks = {};

    function anchor(p) {
        var key = Math.round(p.x) + ":" + Math.round(p.y);
        var n = stacks[key] || 0;
        stacks[key] = n + 1;
        return { x: p.x, y: p.y + n * StackStep };
    }

    for (var i = 0; i < (rows ? rows.length : 0); ++i) {
        var r = rows[i];
        var id = String(r.id || "");
        if (!modeApplies(r.mode, mode))
            continue;
        var triggers = r.triggers || [];
        var label = {
            id: id,
            label: String(r.label || id),
            description: String(r.description || ""),
            chord: triggers.length ? chordText(triggers[0]) : "",
            assigned: !!r.assigned && triggers.length > 0
        };
        var point = null;
        var dir = "";
        for (var e = 0; e < EdgeVerbs.length && !dir; ++e)
            dir = _direction(id, EdgeVerbs[e]);
        if (dir) {
            if (focused)
                point = _edgePoint(focused, dir);
        } else {
            for (var g = 0; g < GapVerbs.length && !dir; ++g)
                dir = _direction(id, GapVerbs[g]);
            if (dir) {
                if (focused)
                    point = _gapPoint(cells, focused, dir);
            } else if (id.indexOf(ZonePrefix) === 0) {
                var n = parseInt(id.substring(ZonePrefix.length), 10);
                var z = zoneCell(cells, n);
                if (z)
                    point = { x: z.x + z.w / 2, y: z.y + z.h / 2 };
                else
                    // A digit past the last cell has nothing to sit on.
                    continue;
            } else if (CentreVerbs[id]) {
                if (focused)
                    point = { x: focused.x + focused.w / 2, y: focused.y + focused.h / 2 };
            } else {
                column.push(label);
                continue;
            }
        }
        if (!point)
            // A spatial chord with no focused cell to hang on: the column
            // keeps it visible.
            column.push(label);
        else {
            var a = anchor(point);
            label.x = a.x;
            label.y = a.y;
            spatial.push(label);
        }
    }
    return { spatial: spatial, column: column };
}

// Case-insensitive filter over label, chord and id.
function matches(label, filter) {
    if (!filter)
        return true;
    var f = String(filter).toLowerCase();
    return label.label.toLowerCase().indexOf(f) >= 0 || label.chord.toLowerCase().indexOf(f) >= 0
        || label.id.toLowerCase().indexOf(f) >= 0;
}
