// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Notifications.ToastHost, the toast stack manager.
//
// Stacks transient notification toasts at the top-right, newest on top.
// Shows up to maxVisible at once and queues the rest; as a toast
// dismisses (timeout, hover-then-leave, or close), the next queued toast
// takes its place. Slide-in / slide-out / reflow are handled by the
// ListView add / remove / displaced transitions (Motion tokens).
//
//   ToastHost {
//       id: toasts
//       anchors.fill: parent
//       rules: dndRules   // optional per-app-rules seam
//   }
//   toasts.show({ appName: "Mail", summary: "New message", body: "..." })
//
// Per-app-rules seam: assign `rules`, an object exposing
//   evaluate(toast) -> { suppress: bool, timeout: int } | null
// ToastHost consults it before showing each toast (suppress drops it;
// timeout overrides the auto-dismiss). The rules editor + persistence are
// Phase 4.3 (Notification center) and wire into this seam without
// touching ToastHost.

import QtQuick
import Phosphor.Theme

Item {
    id: host

    // Most toasts shown at once; extras queue.
    property int maxVisible: 4
    // Default auto-dismiss when a toast doesn't specify one.
    property int defaultTimeout: 5000
    property real spacing: Tokens.spacing_s
    property real margins: Tokens.spacing_l
    // Per-app-rules hook (see header). Null = no rules.
    property var rules: null

    readonly property int activeCount: activeModel.count
    readonly property int queuedCount: priv.queue.length

    signal toastDismissed(int id)

    // Show (or queue) a toast. `toast` is a plain object:
    //   { id?, appName, summary, body, imageSource, urgency, timeout }
    // Returns the toast id, or -1 if a rule suppressed it.
    function show(toast) {
        // Shallow-copy so a rules timeout override (t.timeout = ...) can't
        // mutate the caller's object; Object.assign ignores a null/undefined
        // source, so this also covers a missing argument.
        const t = Object.assign({}, toast);

        // Per-app-rules seam: consult before showing.
        if (host.rules && typeof host.rules.evaluate === "function") {
            const decision = host.rules.evaluate(t);
            if (decision && decision.suppress)
                return -1;
            if (decision && decision.timeout !== undefined)
                t.timeout = decision.timeout;
        }

        const row = {
            "toastId": t.id !== undefined ? t.id : priv.nextId++,
            "appName": t.appName !== undefined ? t.appName : "",
            "summary": t.summary !== undefined ? t.summary : "",
            "body": t.body !== undefined ? t.body : "",
            "imageSource": t.imageSource !== undefined ? String(t.imageSource) : "",
            "urgency": t.urgency !== undefined ? t.urgency : 1,
            "timeout": t.timeout !== undefined ? t.timeout : host.defaultTimeout
        };

        if (activeModel.count < host.maxVisible)
            // Newest shown on top of the visible stack.
            activeModel.insert(0, row);
        else
            // Overflow queues FIFO (promoted to the bottom later). Reassign
            // (not push) so the queuedCount binding re-evaluates; an
            // in-place Array.push doesn't notify QML.
            priv.queue = priv.queue.concat([row]);

        return row.toastId;
    }

    // Dismiss a toast by id (whether shown or still queued).
    function dismiss(id) {
        for (let i = 0; i < activeModel.count; ++i) {
            if (activeModel.get(i).toastId === id) {
                activeModel.remove(i);
                host.toastDismissed(id);
                priv.promote();
                return;
            }
        }
        for (let j = 0; j < priv.queue.length; ++j) {
            if (priv.queue[j].toastId === id) {
                // Reassign so queuedCount updates (see show()).
                priv.queue = priv.queue.slice(0, j).concat(priv.queue.slice(j + 1));
                // A queued toast that is dismissed also "left"; emit so the
                // contract (toastDismissed fires whenever a toast leaves)
                // holds for queued toasts, not just visible ones.
                host.toastDismissed(id);
                return;
            }
        }
    }

    // Clear everything (e.g. a session lock about to show). Emits
    // toastDismissed for every toast removed (visible and queued) so the
    // signal contract holds for bulk teardown too.
    function clear() {
        const ids = [];
        for (let i = 0; i < activeModel.count; ++i)
            ids.push(activeModel.get(i).toastId);
        for (let j = 0; j < priv.queue.length; ++j)
            ids.push(priv.queue[j].toastId);
        priv.queue = [];
        activeModel.clear();
        for (let k = 0; k < ids.length; ++k)
            host.toastDismissed(ids[k]);
    }

    QtObject {
        id: priv

        property var queue: []
        property int nextId: 1

        // Move the oldest queued toast into the visible set when a slot
        // frees up. Appended to the bottom so it slots in below the
        // current stack.
        function promote() {
            if (priv.queue.length > 0 && activeModel.count < host.maxVisible) {
                const next = priv.queue[0];
                // Reassign so queuedCount updates (see show()).
                priv.queue = priv.queue.slice(1);
                activeModel.append(next);
            }
        }
    }

    ListModel {
        id: activeModel
    }

    ListView {
        id: list

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: host.margins
        anchors.rightMargin: host.margins
        width: 360
        height: Math.min(contentHeight, parent.height - host.margins * 2)
        spacing: host.spacing
        interactive: false
        clip: false
        model: activeModel

        delegate: Toast {
            // Bind Toast's own properties from the model roles. Do NOT
            // redeclare them as `required property` here: Toast already
            // defines appName/summary/body/imageSource/urgency/timeout, so
            // redeclaring would shadow them and the role data would never
            // reach the card (only the always-present close button shows).
            required property var model

            width: list.width
            appName: model.appName
            summary: model.summary
            body: model.body
            imageSource: model.imageSource
            urgency: model.urgency
            timeout: model.timeout
            onDismissed: host.dismiss(model.toastId)
        }

        // Slide in from the right + fade.
        add: Transition {
            NumberAnimation {
                property: "x"
                from: list.width
                duration: Motion.duration_medium_2
                easing: Motion.emphasized
            }
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Motion.duration_short_3
            }
        }

        // Slide out to the right + fade.
        remove: Transition {
            NumberAnimation {
                property: "x"
                to: list.width
                duration: Motion.duration_short_4
                easing: Motion.standard
            }
            NumberAnimation {
                property: "opacity"
                to: 0
                duration: Motion.duration_short_4
            }
        }

        // Reflow remaining toasts when one leaves.
        displaced: Transition {
            NumberAnimation {
                property: "y"
                duration: Motion.duration_short_4
                easing: Motion.standard
            }
        }
    }
}
