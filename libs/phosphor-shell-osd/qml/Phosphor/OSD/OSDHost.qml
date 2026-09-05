// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.OSDHost, the on-screen-display surface manager.
//
// One OSDHost owns the transient OSD surface for a single screen: it
// shows one OSD at a time (volume, brightness, mic, caps-lock, ...),
// holds it for a timeout, and retracts it. A repeated trigger of the
// same OSD updates the value and restarts the hold timer instead of
// recreating the surface (debounce/dedupe), so spinning a volume key
// keeps one OSD alive and refreshed.
//
// OSDs are EDGE BANDS (A3 §4): the delegate declares which screen edge
// it lives on (`edge`) and the host places its frame along that edge,
// inset by the work-area gap. A kind swap never hard-cuts: the outgoing
// band retracts while the incoming one draws out.
//
// Delegates are supplied by a `provider` object exposing
//   createOSD(kind, parent) -> Item
// In the shell that provider is backed by a Registry<IOSDFactory>; in a
// test it can be any QML object with that method. The created delegate is
// expected to carry `value` (0..100) and/or `active` (bool) properties;
// OSDHost sets whichever exist, and reads `edge` and writes `reveal` when
// the delegate carries them.
//
// Multi-screen routing: each OSDHost has a `screenName`. show()'s
// `targetScreen` argument routes a trigger to one screen (or "" / omitted
// = every screen).

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property string screenName: ""
    property int holdDuration: 1500
    property var provider: null
    // Inset from the screen edges: the engines' outer gap, so a band sits
    // where a window's edge would.
    property real edgeMargin: Tokens.spacing_s
    // Room the bar takes at the top, so a top-edge band sits under it.
    property real topInset: Tokens.bar_thickness
    // Kept for hosts that set it; bands no longer float.
    property real bottomMargin: 0

    readonly property alias currentKind: priv.currentKind

    signal shown(string kind)
    signal hidden(string kind)

    function show(kind, value, active, targetScreen) {
        if (targetScreen !== undefined && targetScreen !== "" && targetScreen !== root.screenName)
            return false;
        if (!kind) {
            console.warn("OSDHost: empty kind ignored");
            return false;
        }
        if (!root.provider || typeof root.provider.createOSD !== "function") {
            console.warn("OSDHost: no valid provider set; cannot show", kind);
            return false;
        }

        if (kind === priv.currentKind && priv.delegate) {
            priv.apply(priv.delegate, value, active);
            root.state = "shown";
            holdTimer.restart();
            return true;
        }

        const item = root.provider.createOSD(kind, frame);
        if (!item) {
            console.warn("OSDHost: provider returned no delegate for", kind);
            return false;
        }
        const previousKind = priv.currentKind;
        // The outgoing band retracts on its own while the new one enters,
        // so a volume → brightness swap never hard-cuts.
        priv.retireDelegate();
        priv.delegate = item;
        priv.currentKind = kind;
        priv.apply(item, value, active);
        priv.place(item);
        root.state = "shown";
        holdTimer.restart();
        root.shown(kind);
        if (previousKind !== "")
            root.hidden(previousKind);
        return true;
    }

    function hide() {
        if (priv.currentKind === "")
            return;
        holdTimer.stop();
        root.state = "hidden";
    }

    QtObject {
        id: priv

        property string currentKind: ""
        property Item delegate: null

        function apply(item, value, active) {
            if (value !== undefined && item.value !== undefined)
                item.value = value;
            if (active !== undefined && item.active !== undefined)
                item.active = active;
        }

        // Size the delegate to the edge it declares. Delegates without an
        // `edge` sit along the bottom.
        function place(item) {
            const edge = item.edge !== undefined ? item.edge : 0;
            if (edge === 2) {
                // Right edge, vertical.
                item.width = 48;
                item.height = Qt.binding(() => frame.height - root.topInset - 2 * root.edgeMargin);
                item.x = Qt.binding(() => frame.width - item.width - root.edgeMargin);
                item.y = Qt.binding(() => root.topInset + root.edgeMargin);
            } else if (edge === 1) {
                // Top edge, under the bar.
                item.width = Qt.binding(() => frame.width - 2 * root.edgeMargin);
                item.height = 40;
                item.x = Qt.binding(() => root.edgeMargin);
                item.y = Qt.binding(() => root.topInset + root.edgeMargin);
            } else {
                item.width = Qt.binding(() => frame.width - 2 * root.edgeMargin);
                item.height = 40;
                item.x = Qt.binding(() => root.edgeMargin);
                item.y = Qt.binding(() => frame.height - item.height - root.edgeMargin);
            }
        }

        // Hand the current delegate to a retract animation and drop it
        // afterwards. Deferred out of the current call: show() can be
        // invoked synchronously from C++ and destroying inline would
        // re-enter the engine mid-marshal.
        function retireDelegate() {
            if (priv.delegate) {
                const old = priv.delegate;
                priv.delegate = null;
                if (old.reveal !== undefined) {
                    retire.target = old;
                    retire.restart();
                } else {
                    Qt.callLater(function () {
                        priv.destroyItem(old);
                    });
                }
            }
            priv.currentKind = "";
        }

        function destroyItem(item) {
            if (!item)
                return;
            try {
                item.destroy();
            } catch (e) {
                console.warn("OSDHost: delegate is not destroyable; the provider must return a JS-owned item (QQmlEngine::JavaScriptOwnership). Leaking it.", e);
            }
        }

        function onHidden() {
            const k = priv.currentKind;
            retireDelegate();
            if (k !== "")
                root.hidden(k);
        }
    }

    // Retract for a delegate that has been replaced or timed out, then
    // destroy it.
    SequentialAnimation {
        id: retire

        property Item target: null

        NumberAnimation {
            target: retire.target
            property: "reveal"
            to: 0
            duration: 600
            easing: Motion.release
        }
        ScriptAction {
            script: {
                const old = retire.target;
                retire.target = null;
                priv.destroyItem(old);
            }
        }
    }

    // The band holder: the whole screen, so a delegate can sit on any edge.
    Item {
        id: frame

        anchors.fill: parent
    }

    Timer {
        id: holdTimer

        interval: root.holdDuration
        repeat: false
        onTriggered: root.state = "hidden"
    }

    state: "hidden"
    states: [
        State {
            name: "shown"
        },
        State {
            name: "hidden"
        }
    ]
    transitions: [
        Transition {
            to: "shown"
            // The band draws out from its source point.
            ScriptAction {
                script: {
                    if (priv.delegate && priv.delegate.reveal !== undefined) {
                        enter.target = priv.delegate;
                        enter.restart();
                    }
                }
            }
        },
        Transition {
            to: "hidden"
            ScriptAction {
                script: priv.onHidden()
            }
        }
    ]

    NumberAnimation {
        id: enter

        property: "reveal"
        from: 0
        to: 1
        duration: Motion.duration_enter_content
        easing: Motion.reveal
    }
}
