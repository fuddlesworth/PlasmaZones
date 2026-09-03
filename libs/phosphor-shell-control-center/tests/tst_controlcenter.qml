// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Contract tests for the Phosphor.ControlCenter surface.
//
// These pin the host's registry-agnostic behaviour: how it materialises
// tiles through a provider, what it does with a tile the provider refuses,
// and the detail-view routing. The provider here is a plain QML object
// with createTile(id, parent), which is exactly the seam the shell fills
// with a Registry<IControlCenterTileFactory>-backed controller.
//
// Visual behaviour (tile chrome, ripple, panel slide) is not asserted
// here; it belongs to the demo and to phosphor-shell-widgets' own tests.

import QtQuick
import QtTest
import Phosphor.ControlCenter

TestCase {
    id: testCase

    name: "PhosphorControlCenter"

    Component {
        id: controlCenterComp

        ControlCenter {}
    }

    Component {
        id: tileComp

        Tile {}
    }

    // A provider that builds a real Tile for every id except those in
    // `unavailable`, for which it returns null the way a factory reports
    // "no service / no hardware here".
    QtObject {
        id: fakeProvider

        property var unavailable: []
        property var requested: []

        function createTile(id, parent) {
            fakeProvider.requested.push(id);
            if (fakeProvider.unavailable.indexOf(id) !== -1)
                return null;
            return tileComp.createObject(parent, {
                "label": id
            });
        }
    }

    function init() {
        fakeProvider.unavailable = [];
        fakeProvider.requested = [];
    }

    function test_defaults() {
        const cc = createTemporaryObject(controlCenterComp, testCase);
        verify(cc, "ControlCenter instantiates");
        compare(cc.detailTileId, "", "no detail view at rest");
        compare(cc.columns, 2, "two columns by default");
    }

    function test_materialises_each_tile_id_in_order() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": fakeProvider,
            "tileIds": ["network", "bluetooth", "audio"]
        });
        verify(cc, "ControlCenter instantiates");
        compare(fakeProvider.requested, ["network", "bluetooth", "audio"], "tiles built in the declared order");
    }

    function test_null_from_provider_is_not_an_error() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": fakeProvider
        });
        fakeProvider.unavailable = ["bluetooth"];
        const seen = [];
        cc.tileResolved.connect(function (tileId, created) {
            seen.push(tileId + ":" + created);
        });
        cc.tileIds = ["network", "bluetooth"];
        compare(seen, ["network:true", "bluetooth:false"], "an unavailable tile reports created=false rather than throwing");
    }

    function test_detail_opens_only_for_a_materialised_tile() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": fakeProvider,
            "tileIds": ["network"]
        });
        compare(cc.openDetail("network"), true, "a live tile can be drilled into");
        compare(cc.detailTileId, "network", "detail view tracks the tile");
        // A tile the provider refused has no instance, so there is nothing
        // to show: opening its detail must fail rather than present an
        // empty panel.
        compare(cc.openDetail("bluetooth"), false, "an unknown tile cannot be drilled into");
        compare(cc.detailTileId, "network", "the failed open leaves the current view alone");
    }

    function test_detail_close_round_trips() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": fakeProvider,
            "tileIds": ["network"]
        });
        const opened = [];
        const closed = [];
        cc.detailOpened.connect(function (id) {
            opened.push(id);
        });
        cc.detailClosed.connect(function (id) {
            closed.push(id);
        });

        cc.openDetail("network");
        cc.closeDetail();
        compare(cc.detailTileId, "", "back to the grid");
        compare(opened, ["network"], "opened announced once");
        compare(closed, ["network"], "closed announced once");

        // Closing again is a no-op, not a second announcement: a host that
        // dismisses on both Escape and a scrim click would otherwise emit
        // twice for one dismissal.
        cc.closeDetail();
        compare(closed, ["network"], "closing an already-closed panel announces nothing");
    }

    function test_rebuild_drops_the_open_detail_view() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "provider": fakeProvider,
            "tileIds": ["network", "audio"]
        });
        cc.openDetail("network");
        compare(cc.detailTileId, "network", "detail open before the rebuild");
        // The detail view belongs to a tile instance the rebuild destroys,
        // so it cannot outlive it.
        cc.tileIds = ["audio"];
        compare(cc.detailTileId, "", "rebuild returns to the grid");
    }

    function test_no_provider_builds_nothing() {
        const cc = createTemporaryObject(controlCenterComp, testCase, {
            "tileIds": ["network"]
        });
        verify(cc, "ControlCenter instantiates without a provider");
        compare(cc.detailTileId, "", "nothing to drill into");
        compare(cc.openDetail("network"), false, "no tile exists to open");
    }
}
