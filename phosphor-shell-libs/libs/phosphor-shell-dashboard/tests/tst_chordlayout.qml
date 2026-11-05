// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// ChordLayout.place: given cells in screen pixels, the focused id and a
// chord list, the label positions. Pure JS, no scene.

import QtQuick
import QtTest
import "../qml/Phosphor/Dashboard/ChordLayout.js" as Layout

TestCase {
    id: testCase

    name: "ChordLayout"

    // A 1000x500 screen split left/right, the right half split top/bottom.
    // Zone numbers as a snapping layout carries them.
    readonly property var cells: [
        {
            "id": "a",
            "x": 0,
            "y": 0,
            "w": 500,
            "h": 500,
            "zoneNumber": 1,
            "focused": true
        },
        {
            "id": "b",
            "x": 500,
            "y": 0,
            "w": 500,
            "h": 250,
            "zoneNumber": 2
        },
        {
            "id": "c",
            "x": 500,
            "y": 250,
            "w": 500,
            "h": 250,
            "zoneNumber": 3
        }
    ]

    function row(id, trigger, mode) {
        return {
            "id": id,
            "label": id,
            "triggers": trigger ? [trigger] : [],
            "assigned": !!trigger,
            "mode": mode || "all"
        };
    }

    function byId(list, id) {
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === id)
                return list[i];
        }
        return null;
    }

    function test_move_chords_sit_on_the_focused_edges() {
        const r = Layout.place(cells, "a", [row("move_window_left", "Meta+Left"), row("move_window_right", "Meta+Right"), row("move_window_up", "Meta+Up"), row("move_window_down", "Meta+Down")], 0);
        compare(r.column.length, 0, "every move chord is spatial");
        compare(r.spatial.length, 4);
        const left = byId(r.spatial, "move_window_left");
        compare(left.x, 0);
        compare(left.y, 250);
        compare(left.chord, "Meta ←", "the chord reads as keys");
        verify(left.assigned);
        const right = byId(r.spatial, "move_window_right");
        compare(right.x, 500);
        compare(right.y, 250);
        compare(byId(r.spatial, "move_window_up").y, 0);
        compare(byId(r.spatial, "move_window_down").y, 500);
    }

    function test_swap_stacks_under_move_on_the_same_edge() {
        const r = Layout.place(cells, "a", [row("move_window_left", "Meta+Left"), row("swap_window_left", "Meta+Shift+Left")], 0);
        const move = byId(r.spatial, "move_window_left");
        const swap = byId(r.spatial, "swap_window_left");
        compare(swap.x, move.x);
        compare(swap.y, move.y + Layout.StackStep, "second label on the anchor stacks down");
    }

    function test_zone_chords_centre_on_their_zone() {
        const r = Layout.place(cells, "a", [row("snap_to_zone_2", "Meta+2"), row("snap_to_zone_3", "Meta+3"), row("snap_to_zone_9", "Meta+9")], 0);
        const two = byId(r.spatial, "snap_to_zone_2");
        compare(two.x, 750);
        compare(two.y, 125);
        const three = byId(r.spatial, "snap_to_zone_3");
        compare(three.x, 750);
        compare(three.y, 375);
        verify(byId(r.spatial, "snap_to_zone_9") === null, "a digit past the last zone is dropped");
        verify(byId(r.column, "snap_to_zone_9") === null);
    }

    function test_zone_digits_fall_back_to_reading_order() {
        // Tiling cells carry no zone numbers: the digit is the reading-order
        // index.
        const tiles = [
            {
                "id": "w1",
                "x": 0,
                "y": 0,
                "w": 500,
                "h": 500
            },
            {
                "id": "w2",
                "x": 500,
                "y": 0,
                "w": 500,
                "h": 500
            }
        ];
        const r = Layout.place(tiles, "", [row("snap_to_zone_2", "Meta+2")], 1);
        const two = byId(r.spatial, "snap_to_zone_2");
        compare(two.x, 750);
        compare(two.y, 250);
    }

    function test_focus_chords_sit_in_the_gap_or_outside_an_open_edge() {
        const r = Layout.place(cells, "a", [row("focus_zone_right", "Meta+Alt+Right"), row("focus_zone_left", "Meta+Alt+Left")], 0);
        const right = byId(r.spatial, "focus_zone_right");
        // a's right edge and b's left edge meet at 500: the gap is there.
        compare(right.x, 500);
        compare(right.y, 250);
        const left = byId(r.spatial, "focus_zone_left");
        // Nothing left of a: nudged outward from the edge.
        compare(left.x, -Layout.EdgeNudge);
        compare(left.y, 250);
    }

    function test_column_focus_uses_the_real_gap_between_columns() {
        const cols = [
            {
                "id": "c1",
                "x": 0,
                "y": 0,
                "w": 480,
                "h": 500,
                "focused": true
            },
            {
                "id": "c2",
                "x": 520,
                "y": 0,
                "w": 480,
                "h": 500
            }
        ];
        const r = Layout.place(cols, "", [row("scroll_focus_column_right", "Meta+L", "scrolling")], 2);
        const right = byId(r.spatial, "scroll_focus_column_right");
        compare(right.x, 500, "midpoint of the 480..520 gap");
        compare(right.y, 250);
    }

    function test_float_and_maximize_centre_on_the_focused_cell() {
        const r = Layout.place(cells, "a", [row("toggle_window_float", "Meta+F"), row("scroll_maximize_column", "Meta+M", "scrolling")], 2);
        const f = byId(r.spatial, "toggle_window_float");
        compare(f.x, 250);
        compare(f.y, 250);
        const m = byId(r.spatial, "scroll_maximize_column");
        compare(m.x, 250);
        compare(m.y, 250 + Layout.StackStep);
    }

    function test_non_spatial_chords_go_to_the_column_and_keep_order() {
        const r = Layout.place(cells, "a", [row("open_editor", "Meta+E"), row("layout_picker", ""), row("move_window_left", "Meta+Left")], 0);
        compare(r.column.length, 2);
        compare(r.column[0].id, "open_editor");
        compare(r.column[1].id, "layout_picker");
        verify(!r.column[1].assigned, "an unbound chord is kept, marked");
        compare(r.column[1].chord, "");
    }

    function test_mode_filter_drops_chords_of_other_modes() {
        const rows = [row("move_window_left", "Meta+Left", "all"), row("scroll_maximize_column", "Meta+M", "scrolling"), row("focus_master", "Meta+Return", "autotile"), row("retile", "Meta+R", "managed"), row("layout_picker", "Meta+P", "layouts")];
        const snapping = Layout.place(cells, "a", rows, 0);
        compare(snapping.spatial.length + snapping.column.length, 2, "all + layouts on a snapping screen");
        const tiling = Layout.place(cells, "a", rows, 1);
        compare(tiling.spatial.length + tiling.column.length, 4, "all + autotile + managed + layouts");
        const none = Layout.place(cells, "a", rows, -1);
        compare(none.spatial.length + none.column.length, 1, "only mode-free chords without a placement mode");
    }

    function test_spatial_chords_without_a_focused_cell_fall_to_the_column() {
        const r = Layout.place(cells, "nobody", [row("move_window_left", "Meta+Left")], 0);
        // The cells carry a `focused` flag, so the focused cell is found
        // even when no id is given.
        compare(r.spatial.length, 1);
        const unfocused = cells.map(c => Object.assign({}, c, {
                "focused": false
            }));
        const r2 = Layout.place(unfocused, "", [row("move_window_left", "Meta+Left")], 0);
        compare(r2.spatial.length, 0);
        compare(r2.column.length, 1);
    }

    function test_chord_text_and_filter() {
        compare(Layout.chordText("Meta+Shift+Down"), "Meta Shift ↓");
        compare(Layout.chordText("Meta++"), "Meta +");
        compare(Layout.chordText(""), "");
        const l = {
            "id": "move_window_left",
            "label": "Move Window Left",
            "chord": "Meta ←"
        };
        verify(Layout.matches(l, ""));
        verify(Layout.matches(l, "move"));
        verify(Layout.matches(l, "META"));
        verify(!Layout.matches(l, "zone"));
    }
}
