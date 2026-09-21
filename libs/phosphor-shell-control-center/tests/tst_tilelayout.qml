// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Layout contract between ControlCenter and its tiles.
//
// Every control is a rail that spans the pane (05 §8): a toggle and a
// range both declare `spansRow` true, and the host stretches each one
// to the pane's width. The two-column grid and its column spans are
// gone with the tile look.

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

    function test_rails_span_the_pane() {
        const toggle = createTemporaryObject(toggleComp, testCase);
        const slider = createTemporaryObject(sliderComp, testCase);
        compare(toggle.spansRow, true, "a toggle rail spans the pane");
        compare(slider.spansRow, true, "a range rail spans the pane");
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
        compare(tiles.length, 2, "both rails were materialised into the list");
        for (let k = 0; k < tiles.length; ++k)
            compare(tiles[k].Layout.fillWidth, true, "rail " + k + " fills the pane");
    }
}
