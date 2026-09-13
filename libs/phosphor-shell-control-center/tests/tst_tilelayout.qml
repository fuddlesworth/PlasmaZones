// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Controls keep their natural height in a single-column surface.

import QtQuick
import QtQuick.Layouts
import QtTest
import Phosphor.ControlCenter
import Phosphor.Theme

TestCase {
    id: testCase

    name: "TileLayout"
    when: windowShown
    visible: true
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
                return sliderComp.createObject(parent, {
                    objectName: id
                });
            return toggleComp.createObject(parent, {
                objectName: id
            });
        }
    }

    function test_shelfReflowsWithoutRecreatingControls() {
        AppearanceStore.setValue("presentation", "stage");
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            provider: provider,
            tileIds: ["toggle", "slider"],
            width: 1000,
            height: 400
        });
        const slider = findChild(cc, "slider");
        verify(slider);
        slider.value = 57;
        compare(slider.parent.objectName, "levelsGrid");
        cc.width = 400;
        compare(cc.shelf, false);
        tryVerify(() => slider.mapToItem(cc, 0, 0).y > findChild(cc, "toggle").mapToItem(cc, 0, 0).y);
        compare(findChild(cc, "slider"), slider);
        compare(slider.value, 57);
        cc.width = 1000;
        compare(slider.parent.objectName, "levelsGrid");
        AppearanceStore.setValue("presentation", "navigator");
    }

    function test_overview_style_keeps_the_default_panel_compact() {
        const previous = Appearance.settings.presentation;
        try {
            AppearanceStore.setValue("presentation", "navigator");
            const cc = createTemporaryObject(controlCenterComp, testCase);
            compare(cc.panelWidth, 364);
            compare(cc.shelf, false);
            AppearanceStore.setValue("presentation", "stage");
            wait(0);
            compare(cc.panelWidth, 364);
            compare(cc.shelf, false);
        } finally {
            AppearanceStore.setValue("presentation", previous);
        }
    }

    function test_split_action_does_not_toggle_when_opening_details() {
        const tile = createTemporaryObject(toggleComp, testCase, {
            width: 320,
            height: 58,
            detailPanelId: "network"
        });
        let toggles = 0;
        let details = 0;
        tile.toggled.connect(() => ++toggles);
        tile.detailRequested.connect(() => ++details);
        mouseClick(tile, 300, 29);
        compare(details, 1);
        compare(toggles, 0);
        mouseClick(tile, 100, 29);
        compare(toggles, 1);
        tile.available = false;
        mouseClick(tile, 300, 29);
        mouseClick(tile, 100, 29);
        compare(details, 1);
        compare(toggles, 1);
    }

    function test_controls_span_the_row() {
        const toggle = createTemporaryObject(toggleComp, testCase);
        const slider = createTemporaryObject(sliderComp, testCase);
        compare(toggle.spansRow, true, "a connection spans the row");
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
        function collect(item) {
            for (const child of item.children) {
                if (child.spansRow !== undefined)
                    tiles.push(child);
                else
                    collect(child);
            }
        }
        collect(cc);
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
            compare(tiles[m].Layout.columnSpan, 1, "tile " + m + " spans the single column");
    }
}
