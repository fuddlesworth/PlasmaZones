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

        function createOSD(kind, parent) {
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
    }

    function test_show_creates_delegate() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show("volume", 50, undefined, "");
        compare(h.currentKind, "volume", "currentKind reflects the shown OSD");
        compare(fakeProvider.created, 1, "one delegate created");
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

    Component {
        id: spyComp

        SignalSpy {}
    }
}
