// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Layout contract between a tile and the ControlCenter grid.
//
// A toggle takes one cell; a range takes the whole row. The tile only
// declares which it is (`spansRow`) and the host applies the span, so a
// tile never has to know the column count.
//
// This is pinned because it broke once and unit tests did not catch it:
// SliderTile's documentation claimed it spanned the grid while nothing
// implemented the span, so the grid's implicit width came out at roughly
// twice its container and the second column ran off the edge. Only an
// offscreen render showed it. These cases assert the contract that fix
// introduced.

import QtQuick
import QtQuick.Layouts
import QtTest
import Phosphor.ControlCenter

TestCase {
    id: testCase

    name: "PhosphorControlCenterTileLayout"

    Component {
        id: controlCenterComp

        ControlCenter {}
    }

    Component {
        id: toggleComp

        Tile {}
    }

    Component {
        id: sliderComp

        SliderTile {}
    }

    QtObject {
        id: provider

        function createTile(id, parent) {
            if (id === "slider")
                return sliderComp.createObject(parent, {});
            return toggleComp.createObject(parent, {});
        }
    }

    function test_tiles_declare_their_span() {
        const toggle = createTemporaryObject(toggleComp, testCase);
        const slider = createTemporaryObject(sliderComp, testCase);
        compare(toggle.spansRow, false, "a toggle takes one cell");
        compare(slider.spansRow, true, "a range takes the whole row");
    }

    function test_host_applies_the_span() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": provider,
            "tileIds": ["toggle", "slider"],
            "columns": 2
        });
        verify(cc, "ControlCenter instantiates");

        // The grid is the tiles' parent, so its children are the tiles in
        // declared order (plus the layout's own internals, which are not
        // Tiles and carry no spansRow).
        const tiles = [];
        for (let i = 0; i < cc.children.length; ++i) {
            const layer = cc.children[i];
            for (let j = 0; j < layer.children.length; ++j) {
                const child = layer.children[j];
                if (child.spansRow !== undefined)
                    tiles.push(child);
            }
        }
        compare(tiles.length, 2, "both tiles were materialised into the grid");

        // Every tile stretches to its cell; only the range spans columns.
        for (let k = 0; k < tiles.length; ++k)
            compare(tiles[k].Layout.fillWidth, true, "tile " + k + " fills its cell");
        const spans = tiles.map(t => t.Layout.columnSpan);
        compare(spans.indexOf(2) !== -1, true, "the range tile spans both columns");
        compare(spans.indexOf(1) !== -1, true, "the toggle keeps a single column");
    }

    function test_span_follows_the_column_count() {
        // A three-column grid must give the range three columns, not the
        // two a hard-coded span would have baked in.
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": provider,
            "tileIds": ["slider"],
            "columns": 3
        });
        let span = 0;
        for (let i = 0; i < cc.children.length; ++i) {
            const layer = cc.children[i];
            for (let j = 0; j < layer.children.length; ++j) {
                const child = layer.children[j];
                if (child.spansRow !== undefined)
                    span = child.Layout.columnSpan;
            }
        }
        compare(span, 3, "the span tracks the grid's column count");
    }

    // The case above passes columns as an INITIAL property, so it only ever
    // exercised the construction path through rebuild(). The grid reflows on
    // a live `columns` change, and columnSpan is written imperatively, so
    // without a change handler a spanning tile stays pinned to the old span:
    // a slider built at two columns spanning two of three.
    function test_span_follows_a_live_column_change() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": provider,
            "tileIds": ["slider"],
            "columns": 2
        });
        const spanNow = function () {
            let span = 0;
            for (let i = 0; i < cc.children.length; ++i) {
                const layer = cc.children[i];
                for (let j = 0; j < layer.children.length; ++j) {
                    const child = layer.children[j];
                    if (child.spansRow !== undefined)
                        span = child.Layout.columnSpan;
                }
            }
            return span;
        };
        compare(spanNow(), 2, "starts at the initial column count");

        cc.columns = 4;
        compare(spanNow(), 4, "follows a column count changed after construction");
    }
}
