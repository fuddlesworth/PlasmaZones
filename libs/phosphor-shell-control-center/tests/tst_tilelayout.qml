// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Layout contract between ControlCenter and its tiles.
//
// A toggle is a CARD in a two-column grid; a range (volume, brightness)
// spans both columns, because its 2 px underline is its control and a
// longer line is a finer one to drag. Both fill the width they are given,
// and neither fills the HEIGHT: stretching cards vertically is what turned
// a five-control panel into five slabs when the surface was zone-sized.
//
// This previously asserted the opposite — that every control is a rail
// spanning the pane. That was the shape when the control center was an
// engine-placed pane and took the whole zone; it is a card grid in a
// content-sized transient now.

import QtQuick
import QtQuick.Layouts
import QtTest
import Phosphor.ControlCenter

TestCase {
    id: testCase

    name: "TileLayout"
    when: windowShown
    width: 400
    height: 400

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

    function test_only_ranges_span_both_columns() {
        const toggle = createTemporaryObject(toggleComp, testCase);
        const slider = createTemporaryObject(sliderComp, testCase);
        compare(toggle.spansRow, false, "a toggle is a card in one column");
        compare(slider.spansRow, true, "a range spans both columns");
    }

    function test_host_stretches_every_rail() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": provider,
            "tileIds": ["toggle", "slider"]
        });
        verify(cc, "ControlCenter instantiates");

        // The list is the tiles' parent, so its children are the rails in
        // declared order (plus the layout's own internals, which carry no
        // spansRow).
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
        for (let k = 0; k < tiles.length; ++k) {
            compare(tiles[k].Layout.fillWidth, true, "tile " + k + " fills its column");
            // The one that matters: a card keeps its own height. With
            // fillHeight the grid divides the surface between the cards, and
            // on a tall surface each becomes a slab with a glyph at the top
            // and a label at the bottom.
            compare(tiles[k].Layout.fillHeight, false, "tile " + k + " keeps its own height");
        }
        // A range takes both columns; a toggle takes one.
        for (let m = 0; m < tiles.length; ++m)
            compare(tiles[m].Layout.columnSpan, tiles[m].spansRow ? 2 : 1, "tile " + m + " spans by its kind");
    }
}
