// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Notifications.ToastHost, the toast stack manager.
//
// A toast is a line on the edge of the window it concerns (A3 §3). A
// toast shown with a `windowId` that the screen's placement map has a
// cell for is placed on its own: the 2 px band across that window's top
// edge (the cell's screen rect), the card hanging under it, in the
// window's own hue. Toasts for the same window stack downward inside
// the window's rect, newest on top, and every anchored toast follows
// its window on the map's coalesced changed() (a move, a retile, a
// focus change). A toast without a window, or whose window the map does
// not show, falls back to the work area's top edge, centred, in the
// ListView stack: up to maxVisible at once, the rest queued and promoted
// as slots free up. Bands enter from their centre outward and cards
// enter on opacity; nothing slides in from the side.
//
// The map comes from `placementMap` when the composer binds one, else
// from `PlacementMap.forScreen(screenName)` when the Phosphor.Shell
// singleton is registered in this engine (the shell process; the demo
// and tests have the module but not the singleton, so they fall back to
// the top edge, or bind a fake map).
//
//   ToastHost {
//       id: toasts
//       anchors.fill: parent
//       rules: dndRules   // optional per-app-rules seam
//   }
//   toasts.show({ appName: "Mail", summary: "New message", body: "...",
//                 windowId: "<the daemon's window id>" })
//
// Per-app-rules seam: assign `rules`, an object exposing
//   evaluate(toast) -> { suppress: bool, timeout: int } | null
// ToastHost consults it before showing each toast (suppress drops it;
// timeout overrides the auto-dismiss). The rules editor + persistence are
// Phase 4.3 (Notification center) and wire into this seam without
// touching ToastHost.

import QtQuick
import Phosphor.Shell
import Phosphor.Theme

Item {
    id: host

    // Most toasts shown at once in the screen-edge stack; extras queue.
    // Anchored toasts have their window's real estate and do not count.
    property int maxVisible: 4
    // Default auto-dismiss when a toast doesn't specify one.
    property int defaultTimeout: 5000
    property real spacing: Tokens.spacing_s
    property real margins: Tokens.spacing_l
    // Per-app-rules hook (see header). Null = no rules.
    property var rules: null
    // This screen, for the singleton lookup; the bar's screen name.
    property string screenName: ""
    // This screen's PlacementMapScreen (or any object with its cells /
    // cellRect(id) / changed() surface). Bound by the shell composer;
    // resolved from the singleton when left null.
    property var placementMap: null

    readonly property var _screenMap: typeof PlacementMap !== "undefined" && host.screenName.length > 0 ? PlacementMap.forScreen(host.screenName) : null
    readonly property var activeMap: host.placementMap ? host.placementMap : host._screenMap

    readonly property int activeCount: activeModel.count + anchoredModel.count

    // Where a pointer can land on this host: the stack's rect and every
    // anchored card's, in the host's own coordinates. For a composer that
    // mounts the host on a click-through surface and opens input over the
    // cards alone, so hover (which pauses the timer) and the close button
    // work while the rest of the surface passes clicks through to the
    // windows beneath. Every geometry read here is a binding dependency,
    // so the list re-evaluates as cards come, go and travel with their
    // window; the host fills its surface, so these are surface-local.
    readonly property var inputRects: {
        const rects = [];
        if (activeModel.count > 0 && list.width > 0 && list.height > 0)
            rects.push(Qt.rect(list.x, list.y, list.width, list.height));
        for (let i = 0; i < anchoredRepeater.count; ++i) {
            const item = anchoredRepeater.itemAt(i);
            if (item && item.width > 0 && item.height > 0)
                rects.push(Qt.rect(item.x, item.y, item.width, item.height));
        }
        return rects;
    }
    readonly property int anchoredCount: anchoredModel.count
    readonly property int queuedCount: priv.queue.length

    signal toastDismissed(int id)

    // Show (or queue) a toast. `toast` is a plain object:
    //   { id?, appName, appId?, windowId?, summary, body, imageSource,
    //     urgency, timeout }
    // `appId` is carried for rules and a future bar-entry anchor; only
    // `windowId` (the daemon's window id, the key of a tiling or
    // scrolling cell) anchors the toast to a window today.
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

        // Advance the auto-id counter past any caller-supplied id so a
        // later auto-generated id can't collide with an explicit one (which
        // dismiss() would then resolve to the wrong toast).
        if (t.id !== undefined && t.id >= priv.nextId)
            priv.nextId = t.id + 1;

        const row = {
            "toastId": t.id !== undefined ? t.id : priv.nextId++,
            "appName": t.appName !== undefined ? t.appName : "",
            "appId": t.appId !== undefined ? String(t.appId) : "",
            "windowId": t.windowId !== undefined ? String(t.windowId) : "",
            "summary": t.summary !== undefined ? t.summary : "",
            "body": t.body !== undefined ? t.body : "",
            "imageSource": t.imageSource !== undefined ? String(t.imageSource) : "",
            "urgency": t.urgency !== undefined ? t.urgency : 1,
            "timeout": t.timeout !== undefined ? t.timeout : host.defaultTimeout
        };

        // A window the map shows: the toast hangs from that window's edge.
        if (row.windowId !== "" && priv.rectFor(row.windowId) !== null) {
            anchoredModel.insert(0, row);
            return row.toastId;
        }

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

    // Dismiss a toast by id (whether anchored, shown or still queued).
    function dismiss(id) {
        for (let a = 0; a < anchoredModel.count; ++a) {
            if (anchoredModel.get(a).toastId === id) {
                anchoredModel.remove(a);
                host.toastDismissed(id);
                return;
            }
        }
        for (let i = 0; i < activeModel.count; ++i) {
            if (activeModel.get(i).toastId === id) {
                activeModel.remove(i);
                // Promote the next queued toast before emitting, so a
                // re-entrant toastDismissed handler observes the slot already
                // refilled (matches clear()'s mutate-fully-then-emit order).
                priv.promote();
                host.toastDismissed(id);
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
    // toastDismissed for every toast removed (anchored, visible and
    // queued) so the signal contract holds for bulk teardown too.
    function clear() {
        const ids = [];
        for (let a = 0; a < anchoredModel.count; ++a)
            ids.push(anchoredModel.get(a).toastId);
        for (let i = 0; i < activeModel.count; ++i)
            ids.push(activeModel.get(i).toastId);
        for (let j = 0; j < priv.queue.length; ++j)
            ids.push(priv.queue[j].toastId);
        priv.queue = [];
        anchoredModel.clear();
        activeModel.clear();
        for (let k = 0; k < ids.length; ++k)
            host.toastDismissed(ids[k]);
    }

    // The positioned Item of an anchored toast, or null. For hosts that
    // need its geometry (and the tests).
    function anchoredItem(id) {
        for (let i = 0; i < anchoredRepeater.count; ++i) {
            const item = anchoredRepeater.itemAt(i);
            if (item && item.toastId === id)
                return item;
        }
        return null;
    }

    QtObject {
        id: priv

        property var queue: []
        property int nextId: 1
        // Bumped on every map change so the anchored delegates re-read
        // their window's rect and hue.
        property int mapEpoch: 0

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

        // The window's cell rect in screen pixels, or null when the map
        // has no such cell. Duck-typed so a fake map, or a
        // composer-supplied object, works.
        function rectFor(windowId) {
            const m = host.activeMap;
            if (!m || typeof m.cellRect !== "function" || windowId === "")
                return null;
            const r = m.cellRect(windowId);
            return r && r.width > 0 && r.height > 0 ? r : null;
        }

        // The window's cell (for its hue), or null.
        function cellFor(windowId) {
            const m = host.activeMap;
            if (!m || !m.cells || windowId === "")
                return null;
            const cells = m.cells;
            for (let i = 0; i < cells.length; ++i) {
                if (String(cells[i].id) === windowId)
                    return cells[i];
            }
            return null;
        }

        // Where the delegate at `index` starts under its window's edge:
        // below every newer toast for the same window.
        function stackOffset(index, windowId) {
            let offset = 0;
            for (let j = 0; j < index; ++j) {
                const item = anchoredRepeater.itemAt(j);
                if (item && item.windowId === windowId)
                    offset += item.height + host.spacing;
            }
            return offset;
        }
    }

    // The window moved, or focus changed: anchored toasts follow.
    Connections {
        target: host.activeMap
        ignoreUnknownSignals: true
        function onChanged() {
            priv.mapEpoch++;
        }
    }
    onActiveMapChanged: priv.mapEpoch++

    ListModel {
        id: activeModel
    }

    ListModel {
        id: anchoredModel
    }

    // Toasts on their window's edge. Each positions itself from the map;
    // a window that leaves the map drops its toast to the top edge.
    Repeater {
        id: anchoredRepeater

        model: anchoredModel

        delegate: Toast {
            id: anchored

            required property var model
            required property int index
            readonly property int toastId: model.toastId
            readonly property string windowId: model.windowId
            // mapEpoch is the dependency that re-reads on every map change.
            readonly property var cellRect: priv.mapEpoch >= 0 ? priv.rectFor(windowId) : null
            readonly property var cell: priv.mapEpoch >= 0 ? priv.cellFor(windowId) : null
            readonly property bool anchoredToWindow: cellRect !== null
            readonly property real stackOffset: anchoredModel.count >= 0 && priv.mapEpoch >= 0 ? priv.stackOffset(index, windowId) : 0

            width: anchoredToWindow ? cellRect.width : 360
            x: anchoredToWindow ? cellRect.x : (host.width - width) / 2
            y: (anchoredToWindow ? cellRect.y : host.margins) + stackOffset
            appName: model.appName
            summary: model.summary
            body: model.body
            imageSource: model.imageSource
            urgency: model.urgency
            timeout: model.timeout
            // The window's own hue, so the band agrees with the rail and
            // the map above it.
            t: cell && cell.t !== undefined ? Number(cell.t) : Spectrum.tForX(x + width / 2, host.width)
            onDismissed: host.dismiss(model.toastId)

            // Band from its centre outward, card on opacity, as the stack.
            bandReveal: 0
            opacity: 0
            Component.onCompleted: {
                bandReveal = 1;
                opacity = 1;
            }
            Behavior on bandReveal {
                NumberAnimation {
                    duration: Motion.duration_reveal
                    easing: Motion.reveal
                }
            }
            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.duration_enter_content
                    easing: Motion.reveal
                }
            }
            // Travels with its window rather than re-appearing, on the
            // settle spring (A1 M4): a window still being dragged keeps
            // retargeting one motion instead of restarting a bezier.
            Behavior on x {
                SettleAnimation {}
            }
            Behavior on y {
                SettleAnimation {}
            }
            Behavior on width {
                SettleAnimation {}
            }
        }
    }

    ListView {
        id: list

        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: host.margins
        width: 360
        height: Math.max(0, Math.min(contentHeight, parent.height - host.margins * 2))
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
            t: Spectrum.tForX(list.x + list.width / 2, host.width)
            onDismissed: host.dismiss(model.toastId)
        }

        // Band draws out from its centre, card enters on opacity.
        add: Transition {
            NumberAnimation {
                property: "bandReveal"
                from: 0
                to: 1
                duration: Motion.duration_reveal
                easing: Motion.reveal
            }
            NumberAnimation {
                property: "opacity"
                from: 0
                to: 1
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }

        // Card dismisses, then the band retracts toward its centre.
        remove: Transition {
            SequentialAnimation {
                NumberAnimation {
                    property: "opacity"
                    to: 0
                    duration: Motion.duration_dismiss
                    easing: Motion.dismiss
                }
                NumberAnimation {
                    property: "bandReveal"
                    to: 0
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
        }

        // Reflow remaining toasts when one leaves.
        displaced: Transition {
            NumberAnimation {
                property: "y"
                duration: Motion.duration_release
                easing: Motion.release
            }
        }
    }
}
