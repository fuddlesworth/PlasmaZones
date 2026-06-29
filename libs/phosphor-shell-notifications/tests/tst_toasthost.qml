// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Behaviour tests for ToastHost: show, queue beyond maxVisible,
// dismiss + promote, the toastDismissed signal, clear, and the
// per-app-rules seam (suppress + pass-through). The stacking/transition
// visuals are exercised by the toast demo; this pins the queue logic.

import QtQuick
import QtTest
import Phosphor.Notifications

TestCase {
    id: testCase

    name: "ToastHost"

    // A rule object that suppresses everything (a "do not disturb").
    QtObject {
        id: suppressRules

        function evaluate(toast) {
            return {
                "suppress": true
            };
        }
    }

    Component {
        id: hostComp

        ToastHost {
            width: 480
            height: 480
        }
    }

    function test_show_adds_a_toast() {
        const h = createTemporaryObject(hostComp, testCase);
        const id = h.show({
            "summary": "Hello"
        });
        verify(id > 0, "show returns a positive id");
        compare(h.activeCount, 1, "one toast active");
        compare(h.queuedCount, 0, "nothing queued");
    }

    function test_queue_beyond_max() {
        const h = createTemporaryObject(hostComp, testCase, {
            "maxVisible": 2
        });
        const a = h.show({
            "summary": "a"
        });
        h.show({
            "summary": "b"
        });
        h.show({
            "summary": "c"
        });
        compare(h.activeCount, 2, "only maxVisible shown");
        compare(h.queuedCount, 1, "the rest queue");
        h.dismiss(a);
        compare(h.activeCount, 2, "a queued toast is promoted into the freed slot");
        compare(h.queuedCount, 0, "queue drained");
    }

    function test_dismiss_emits_signal() {
        const h = createTemporaryObject(hostComp, testCase);
        const spy = spyComp.createObject(testCase, {
            "target": h,
            "signalName": "toastDismissed"
        });
        const id = h.show({
            "summary": "x"
        });
        h.dismiss(id);
        compare(spy.count, 1, "toastDismissed fired once");
        compare(spy.signalArguments[0][0], id, "carries the dismissed id");
        compare(h.activeCount, 0, "removed from the active set");
    }

    function test_dismiss_unknown_id_is_safe() {
        const h = createTemporaryObject(hostComp, testCase);
        h.show({
            "summary": "x"
        });
        h.dismiss(99999); // not present
        compare(h.activeCount, 1, "an unknown id leaves the stack untouched");
    }

    function test_clear_removes_all() {
        const h = createTemporaryObject(hostComp, testCase, {
            "maxVisible": 1
        });
        h.show({
            "summary": "a"
        });
        h.show({
            "summary": "b"
        }); // queued
        compare(h.activeCount, 1);
        compare(h.queuedCount, 1);
        h.clear();
        compare(h.activeCount, 0, "active cleared");
        compare(h.queuedCount, 0, "queue cleared");
    }

    function test_rules_suppress() {
        const h = createTemporaryObject(hostComp, testCase);
        h.rules = suppressRules;
        const r = h.show({
            "summary": "blocked"
        });
        compare(r, -1, "a suppressing rule returns -1");
        compare(h.activeCount, 0, "nothing shown");
    }

    function test_rules_passthrough_when_unset() {
        const h = createTemporaryObject(hostComp, testCase);
        const r = h.show({
            "summary": "shown"
        });
        verify(r > 0, "no rules: the toast shows");
        compare(h.activeCount, 1);
    }

    Component {
        id: spyComp

        SignalSpy {}
    }
}
