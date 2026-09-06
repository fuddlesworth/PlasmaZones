// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Behaviour tests for OSDHost: delegate creation, debounce/dedupe (a
// repeated trigger of the same kind reuses the delegate and restarts the
// hold rather than recreating it), kind-swap, per-screen routing, the
// shown/hidden signals, and the auto-hide lifecycle. A fake provider
// stands in for the registry-backed one so the host logic is exercised
// without C++.

import QtQuick
import QtTest
import Phosphor.OSD

TestCase {
    id: testCase

    name: "OSDHost"

    // Trivial OSD delegate with the value/active surface OSDHost sets.
    Component {
        id: fakeDelegate

        Item {
            property real value: 0
            property bool active: false

            implicitWidth: 20
            implicitHeight: 20
        }
    }

    // Registry-free provider: counts creations so the dedupe contract is
    // observable.
    QtObject {
        id: fakeProvider

        property int created: 0
        // Last delegate handed out, so a test can capture it and assert
        // it gets destroyed on swap.
        property var lastItem: null
        // When set, createOSD returns null for this kind, simulating an
        // unregistered/mistyped kind (the registry-backed provider returns
        // null for those).
        property string failFor: ""

        function createOSD(kind, parent) {
            if (kind === fakeProvider.failFor)
                return null;
            fakeProvider.created++;
            // Create JS-owned (no creation parent) then reparent, so the
            // delegate is destroyable by the host — the same ownership the
            // real IOSDFactory path establishes via JavaScriptOwnership.
            const item = fakeDelegate.createObject(null, {});
            item.parent = parent;
            fakeProvider.lastItem = item;
            return item;
        }
    }

    Component {
        id: holderComp

        QtObject {
            // Typed object property: QML nulls it automatically when the
            // referenced QObject is destroyed (a `var` would keep a stale
            // wrapper instead).
            property QtObject ref: null
        }
    }

    // A stand-in for PlacementMapScreen: the property names OSDHost reads,
    // a settable focused cell rect, and the coalesced changed() signal.
    Component {
        id: fakeMapComp

        QtObject {
            property int mode: 1
            property var cells: []
            property var lens: ({})
            property int overflowLeft: 0
            property int overflowRight: 0
            property rect workArea: Qt.rect(0, 28, 400, 272)
            property string focusedId: ""
            property rect focusedRect: Qt.rect(0, 0, 0, 0)

            signal changed

            function focusedCellId() {
                return focusedId;
            }
            function cellRect(id) {
                return id === focusedId ? focusedRect : Qt.rect(0, 0, 0, 0);
            }
        }
    }

    Component {
        id: hostComp

        OSDHost {
            width: 400
            height: 300
            holdDuration: 60
            provider: fakeProvider
        }
    }

    function init() {
        fakeProvider.created = 0;
        fakeProvider.failFor = "";
    }

    function test_show_creates_delegate() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        compare(h.currentKind, "volume", "currentKind reflects the shown OSD");
        compare(fakeProvider.created, 1, "one delegate created");
    }

    function test_failed_swap_preserves_current_osd() {
        const h = createTemporaryObject(hostComp, testCase);
        const spy = createTemporaryObject(spyComp, testCase, {
            "target": h,
            "signalName": "hidden"
        });
        h.show("volume", 50, undefined, "");
        compare(h.currentKind, "volume", "volume is up");
        // A swap whose provider yields no delegate must leave the current
        // OSD intact, return false, and NOT emit hidden for it.
        fakeProvider.failFor = "missing";
        const ok = h.show("missing", undefined, undefined, "");
        compare(ok, false, "a null-delegate show returns false");
        compare(h.currentKind, "volume", "the current OSD survives a failed swap");
        compare(fakeProvider.created, 1, "no new delegate was created");
        compare(spy.count, 0, "hidden is not emitted for a show that failed");
    }

    function test_no_provider_returns_false() {
        const h = createTemporaryObject(hostComp, testCase, {
            "provider": null
        });
        compare(h.show("volume", 50, undefined, ""), false, "no provider -> false");
        compare(h.currentKind, "", "nothing shown without a provider");
    }

    function test_value_and_active_applied_to_delegate() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 42, undefined, "");
        compare(fakeProvider.lastItem.value, 42, "value lands on the delegate");
        h.show("mic", undefined, true, "");
        compare(fakeProvider.lastItem.active, true, "active lands on the delegate");
    }

    function test_repeat_same_kind_dedupes() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        h.show("volume", 70, undefined, "");
        h.show("volume", 90, undefined, "");
        compare(fakeProvider.created, 1, "repeated same-kind shows reuse the one delegate");
        compare(h.currentKind, "volume", "still showing volume");
    }

    function test_different_kind_swaps() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        h.show("brightness", 30, undefined, "");
        compare(fakeProvider.created, 2, "a new kind creates a fresh delegate");
        compare(h.currentKind, "brightness", "currentKind swapped");
    }

    function test_swap_destroys_old_delegate() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        // Capture the first delegate; a var property nulls out when the
        // referenced QObject is destroyed.
        const holder = createTemporaryObject(holderComp, testCase, {
            "ref": fakeProvider.lastItem
        });
        verify(holder.ref, "captured the first delegate");
        h.show("brightness", 30, undefined, "");
        // Teardown is deferred (Qt.callLater); the old delegate must still
        // be destroyed, not leaked behind the new one.
        tryCompare(holder, "ref", null, 3000, "the swapped-out delegate is destroyed, not leaked");
    }

    function test_routing_ignores_other_screen() {
        const h = createTemporaryObject(hostComp, testCase);
        h.screenName = "DP-1";
        h.show("volume", 50, undefined, "DP-2");
        compare(h.currentKind, "", "a trigger for another screen is ignored");
        compare(fakeProvider.created, 0, "no delegate created for the wrong screen");
        h.show("volume", 50, undefined, "DP-1");
        compare(h.currentKind, "volume", "a trigger for this screen shows");
        compare(fakeProvider.created, 1, "delegate created for the matching screen");
    }

    function test_empty_target_hits_every_host() {
        const h = createTemporaryObject(hostComp, testCase);
        h.screenName = "DP-1";
        h.show("mic", undefined, true, "");
        compare(h.currentKind, "mic", "an empty targetScreen routes to every host");
    }

    function test_shown_signal_fires() {
        const h = createTemporaryObject(hostComp, testCase);
        const spy = createTemporaryObject(spyComp, testCase, {
            "target": h,
            "signalName": "shown"
        });
        h.show("volume", 50, undefined, "");
        compare(spy.count, 1, "shown fired once");
        compare(spy.signalArguments[0][0], "volume", "shown carries the kind");
    }

    function test_auto_hide_clears_after_hold() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        compare(h.state, "shown", "state is shown right after show()");
        // Hold timer (60ms) then the fade-out transition; the delegate is
        // destroyed and currentKind cleared at the end of the transition.
        tryCompare(h, "currentKind", "", 3000, "auto-hides and clears after the hold");
    }

    function test_empty_kind_is_rejected() {
        const h = createTemporaryObject(hostComp, testCase);
        const r = h.show("", 50, undefined, "");
        compare(r, false, "an empty kind returns false");
        compare(h.currentKind, "", "nothing is shown");
        compare(fakeProvider.created, 0, "no delegate created for an empty kind");
    }

    function test_show_returns_bool() {
        const h = createTemporaryObject(hostComp, testCase);
        compare(h.show("volume", 50, undefined, ""), true, "a shown OSD returns true");
        compare(h.show("volume", 60, undefined, "DP-9"), false, "a trigger routed elsewhere returns false");
    }

    function test_hide_dismisses_early() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        h.hide();
        tryCompare(h, "currentKind", "", 3000, "hide() tears the OSD down");
    }

    function test_hidden_signal_on_auto_hide() {
        const h = createTemporaryObject(hostComp, testCase);
        const spy = createTemporaryObject(spyComp, testCase, {
            "target": h,
            "signalName": "hidden"
        });
        h.show("volume", 50, undefined, "");
        tryCompare(spy, "count", 1, 3000, "hidden fires once on auto-hide");
        compare(spy.signalArguments[0][0], "volume", "hidden carries the kind that left");
    }

    function test_swap_emits_hidden_for_previous() {
        const h = createTemporaryObject(hostComp, testCase);
        const spy = createTemporaryObject(spyComp, testCase, {
            "target": h,
            "signalName": "hidden"
        });
        h.show("volume", 50, undefined, "");
        h.show("brightness", 30, undefined, ""); // swap
        compare(spy.count, 1, "swapping emits hidden for the outgoing OSD");
        compare(spy.signalArguments[0][0], "volume", "hidden carries the previous kind");
    }

    function test_reentrant_show_from_hidden_is_symmetric() {
        // A consumer may call show() synchronously from a hidden() handler.
        // Every installed delegate must still get exactly one shown() and,
        // once replaced, exactly one hidden() — no orphan/stale signal.
        const h = createTemporaryObject(hostComp, testCase);
        const shownSpy = createTemporaryObject(spyComp, testCase, {
            "target": h,
            "signalName": "shown"
        });
        const hiddenSpy = createTemporaryObject(spyComp, testCase, {
            "target": h,
            "signalName": "hidden"
        });
        // When "volume" leaves, re-enter with a third kind mid-swap.
        h.hidden.connect(function (k) {
            if (k === "volume")
                h.show("mic", undefined, true, "");
        });
        h.show("volume", 50, undefined, ""); // shown(volume)
        h.show("brightness", 30, undefined, ""); // shown(brightness), hidden(volume) -> show(mic): shown(mic), hidden(brightness)
        compare(h.currentKind, "mic", "the re-entrant kind ends up current");
        compare(shownSpy.count, 3, "volume, brightness, mic each announced shown once");
        compare(hiddenSpy.count, 2, "volume and brightness each announced hidden once");
    }

    // A3 §4: a bottom band sits on the FOCUSED WINDOW's bottom edge, read
    // from the placement map, not on the screen edge.
    function test_band_anchors_to_focused_cell_rect() {
        const map = createTemporaryObject(fakeMapComp, testCase, {
            "focusedId": "w1",
            "focusedRect": Qt.rect(100, 50, 200, 100)
        });
        const h = createTemporaryObject(hostComp, testCase, {
            "placementMap": map,
            "edgeMargin": 8
        });
        h.show("volume", 50, undefined, "");
        const band = fakeProvider.lastItem;
        compare(h.anchoredToWindow, true, "a focused cell anchors the band");
        compare(band.x, 100, "band starts at the window's left edge");
        compare(band.width, 200, "band spans the window's width");
        compare(band.y, 150 - band.height, "band sits on the window's bottom edge");
    }

    function test_band_travels_with_focus_while_shown() {
        const map = createTemporaryObject(fakeMapComp, testCase, {
            "focusedId": "w1",
            "focusedRect": Qt.rect(100, 50, 200, 100)
        });
        const h = createTemporaryObject(hostComp, testCase, {
            "placementMap": map,
            "holdDuration": 5000
        });
        h.show("volume", 50, undefined, "");
        const band = fakeProvider.lastItem;
        compare(band.x, 100);
        // Focus moves to another window: the band follows on the map's
        // coalesced changed(), animating rather than re-appearing.
        map.focusedId = "w2";
        map.focusedRect = Qt.rect(10, 20, 300, 120);
        map.changed();
        tryCompare(band, "x", 10, 3000, "band travelled to the new window's x");
        tryCompare(band, "width", 300, 3000, "band spans the new window's width");
        tryCompare(band, "y", 140 - band.height, 3000, "band sits on the new window's bottom edge");
    }

    function test_band_falls_back_to_screen_edge_without_focus() {
        const map = createTemporaryObject(fakeMapComp, testCase, {
            "focusedId": ""
        });
        const h = createTemporaryObject(hostComp, testCase, {
            "placementMap": map,
            "edgeMargin": 8
        });
        h.show("volume", 50, undefined, "");
        const band = fakeProvider.lastItem;
        compare(h.anchoredToWindow, false, "no focused cell: no anchor");
        compare(band.x, 8, "band inset from the screen's left edge");
        compare(band.width, 400 - 16, "band spans the screen minus the gap");
        compare(band.y, 300 - band.height - 8, "band sits on the screen's bottom edge");
    }

    function test_band_falls_back_without_a_map() {
        // No placementMap bound and no Phosphor.Shell singleton in this
        // engine: the screen edge, as in phase 1.
        const h = createTemporaryObject(hostComp, testCase, {
            "screenName": "DP-1",
            "edgeMargin": 8
        });
        h.show("volume", 50, undefined, "");
        compare(h.anchoredToWindow, false);
        compare(fakeProvider.lastItem.x, 8);
    }

    Component {
        id: spyComp

        SignalSpy {}
    }
}
