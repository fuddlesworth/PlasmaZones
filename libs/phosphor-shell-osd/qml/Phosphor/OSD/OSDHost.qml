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
// OSDs are EDGE BANDS (A3 §4): the delegate declares which edge it lives
// on (`edge`) and the host places its frame along that edge. A bottom or
// top band sits on the FOCUSED WINDOW's edge, read from the screen's
// placement map (`cellRect(focusedCellId())`), and travels with focus
// while it is up: a focus change re-anchors it and the band's x/y/width
// animate to the new window rather than re-appearing. With no focused
// cell, or no map (no daemon, or a host outside the shell), the band
// falls back to the screen edge inset by the work-area gap. A right-edge
// band (brightness) always spans the screen's edge, since it concerns
// the physical panel. A kind swap never hard-cuts: the outgoing band
// retracts while the incoming one draws out.
//
// The map comes from `placementMap` when the composer binds one, else
// from `PlacementMap.forScreen(screenName)` when the Phosphor.Shell
// singleton is registered in this engine (it is in the shell process;
// the demos and tests have the module but not the singleton, so they
// fall back to the screen edge, or bind a fake map).
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
import Phosphor.Shell
import Phosphor.Theme

Item {
    id: root

    property string screenName: ""
    property int holdDuration: 1500
    property var provider: null
    // This screen's PlacementMapScreen (or any object with its
    // focusedCellId() / cellRect(id) / changed() surface). Bound by the
    // shell composer; resolved from the singleton when left null.
    property var placementMap: null
    // Inset from the screen edges: the engines' outer gap, so a band sits
    // where a window's edge would.
    property real edgeMargin: Tokens.spacing_s
    // Room the bar takes at the top, so a top-edge band sits under it.
    property real topInset: Tokens.bar_thickness
    // Kept for hosts that set it; bands no longer float.
    property real bottomMargin: 0

    readonly property alias currentKind: priv.currentKind
    // Whether the current band sits on a window edge rather than the
    // screen edge.
    readonly property alias anchoredToWindow: priv.hasAnchor

    // `typeof` on the singleton name is the engine-agnostic probe: an
    // engine without the ShellEngine registration resolves it undefined
    // rather than throwing.
    readonly property var _screenMap: typeof PlacementMap !== "undefined" && root.screenName.length > 0 ? PlacementMap.forScreen(root.screenName) : null
    readonly property var activeMap: root.placementMap ? root.placementMap : root._screenMap

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
            priv.updateAnchor();
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
        // Anchor before the delegate exists so the first placement lands
        // without a travel animation.
        priv.updateAnchor();
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

        // The focused window's rect in screen pixels, when the map has
        // one. The three animated fields make a shown band travel.
        property bool hasAnchor: false
        property real anchorX: 0
        property real anchorY: 0
        property real anchorW: 0
        property real anchorH: 0

        Behavior on anchorX {
            enabled: priv.delegate !== null
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
        Behavior on anchorY {
            enabled: priv.delegate !== null
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
        Behavior on anchorW {
            enabled: priv.delegate !== null
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
        Behavior on anchorH {
            enabled: priv.delegate !== null
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }

        function apply(item, value, active) {
            if (value !== undefined && item.value !== undefined)
                item.value = value;
            if (active !== undefined && item.active !== undefined)
                item.active = active;
        }

        // Re-read the focused cell's rect from the map. Duck-typed so a
        // fake map in a test, or a composer-supplied object, works.
        function updateAnchor() {
            const m = root.activeMap;
            let r = null;
            if (m && typeof m.focusedCellId === "function" && typeof m.cellRect === "function") {
                const id = m.focusedCellId();
                if (id) {
                    const rc = m.cellRect(id);
                    if (rc && rc.width > 0 && rc.height > 0)
                        r = rc;
                }
            }
            if (r) {
                priv.anchorX = r.x;
                priv.anchorY = r.y;
                priv.anchorW = r.width;
                priv.anchorH = r.height;
            }
            priv.hasAnchor = r !== null;
        }

        // Size the delegate to the edge it declares. Delegates without an
        // `edge` sit along the bottom. Bottom and top follow the anchor
        // (the focused window) when there is one.
        function place(item) {
            const edge = item.edge !== undefined ? item.edge : 0;
            if (edge === 2) {
                // Right edge, vertical: the screen's edge.
                item.width = 48;
                item.height = Qt.binding(() => frame.height - root.topInset - 2 * root.edgeMargin);
                item.x = Qt.binding(() => frame.width - item.width - root.edgeMargin);
                item.y = Qt.binding(() => root.topInset + root.edgeMargin);
            } else if (edge === 1) {
                // Top edge: the focused window's, else under the bar.
                item.width = Qt.binding(() => priv.hasAnchor ? priv.anchorW : frame.width - 2 * root.edgeMargin);
                item.height = 40;
                item.x = Qt.binding(() => priv.hasAnchor ? priv.anchorX : root.edgeMargin);
                item.y = Qt.binding(() => priv.hasAnchor ? priv.anchorY : root.topInset + root.edgeMargin);
            } else {
                // Bottom edge: the focused window's, else the screen's.
                item.width = Qt.binding(() => priv.hasAnchor ? priv.anchorW : frame.width - 2 * root.edgeMargin);
                item.height = 40;
                item.x = Qt.binding(() => priv.hasAnchor ? priv.anchorX : root.edgeMargin);
                item.y = Qt.binding(() => priv.hasAnchor ? priv.anchorY + priv.anchorH - item.height : frame.height - item.height - root.edgeMargin);
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

    // Focus moved, or the window moved: the band follows (A3 §4 c).
    Connections {
        target: root.activeMap
        ignoreUnknownSignals: true
        function onChanged() {
            priv.updateAnchor();
        }
    }
    onActiveMapChanged: priv.updateAnchor()

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
