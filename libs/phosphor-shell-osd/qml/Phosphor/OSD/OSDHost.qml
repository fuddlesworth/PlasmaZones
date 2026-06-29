// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.OSDHost, the on-screen-display surface manager.
//
// One OSDHost owns the transient OSD surface for a single screen: it
// shows one OSD at a time (volume, brightness, mic, caps-lock, ...),
// holds it for a timeout, and fades it out. A repeated trigger of the
// same OSD updates the value and restarts the hold timer instead of
// recreating the surface (debounce/dedupe), so spinning a volume key
// keeps one OSD alive and refreshed.
//
// Delegates are supplied by a `provider` object exposing
//   createOSD(kind, parent) -> Item
// In the shell that provider is backed by a Registry<IOSDFactory>; in a
// test it can be any QML object with that method. The created delegate is
// expected to carry `value` (0..100) and/or `active` (bool) properties;
// OSDHost sets whichever exist.
//
// Multi-screen routing: each OSDHost has a `screenName`. show()'s
// `targetScreen` argument routes a trigger to one screen (or "" / omitted
// = every screen). Compose one OSDHost per monitor via Phosphor.Shell's
// PerScreen and pass the same trigger to all; only the addressed host(s)
// react.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    // This host's screen identifier, for routing. A show() with a specific
    // targetScreen reaches only the host whose screenName matches it; a
    // show() with an empty/omitted targetScreen broadcasts to every host.
    // The wildcard is the empty targetScreen, NOT the empty screenName: a
    // host left at the default "" therefore reacts only to broadcast shows.
    property string screenName: ""
    // How long the OSD stays fully shown before it fades out (ms).
    property int holdDuration: 1500
    // Delegate source: an object with createOSD(kind, parent) -> Item.
    property var provider: null
    // Distance from the bottom edge the OSD floats at.
    property real bottomMargin: 64

    // The kind currently shown, or "" when hidden. Read-only for consumers.
    readonly property alias currentKind: priv.currentKind

    // Emitted when an OSD becomes visible / fully hidden. Useful for
    // tests and for a host that wants to coordinate (e.g. pause an idle
    // timer while an OSD is up).
    signal shown(string kind)
    signal hidden(string kind)

    // Show (or refresh) the OSD for `kind`. value (0..100) and active
    // (bool) are applied to the delegate if it exposes them; pass
    // undefined to leave a property untouched. targetScreen routes the
    // trigger: "" / undefined hits every host, otherwise only the host
    // whose screenName matches. Returns true when an OSD was shown or
    // refreshed on this host, false when the trigger was routed elsewhere,
    // rejected (empty kind, no provider), or the provider had no delegate.
    function show(kind, value, active, targetScreen) {
        if (targetScreen !== undefined && targetScreen !== "" && targetScreen !== root.screenName)
            return false;
        // An empty kind would alias priv.currentKind's "nothing shown"
        // sentinel, producing an OSD that hide()/onHidden refuse to dismiss
        // and never announce. Reject it at the boundary.
        if (!kind) {
            console.warn("OSDHost: empty kind ignored");
            return false;
        }
        if (!root.provider || typeof root.provider.createOSD !== "function") {
            console.warn("OSDHost: no valid provider set; cannot show", kind);
            return false;
        }

        if (kind === priv.currentKind && priv.delegate) {
            // Dedupe: same OSD already up. Update, re-assert "shown" (a
            // repeat during the fade-out window must interrupt the fade and
            // animate back in, not let it finish), and restart the hold.
            priv.apply(priv.delegate, value, active);
            root.state = "shown";
            holdTimer.restart();
            return true;
        }

        // Different kind (or nothing showing): swap in a fresh delegate.
        // Build and validate the replacement BEFORE tearing the current OSD
        // down. If the provider has no delegate for `kind` (an unregistered
        // or mistyped kind), the OSD the user is looking at must stay put;
        // destroying it first would blank the surface and wrongly emit
        // hidden() for a show() that ultimately failed.
        const item = root.provider.createOSD(kind, frame);
        if (!item) {
            console.warn("OSDHost: provider returned no delegate for", kind);
            return false;
        }
        // The outgoing OSD (if any) "left", so announce its hidden() to
        // keep the shown/hidden pairing symmetric for consumers.
        const previousKind = priv.currentKind;
        priv.destroyDelegate();
        if (previousKind !== "")
            root.hidden(previousKind);
        priv.delegate = item;
        priv.currentKind = kind;
        priv.apply(item, value, active);
        root.state = "shown";
        holdTimer.restart();
        root.shown(kind);
        return true;
    }

    // Hide the current OSD early (e.g. the user dismissed it).
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

        // Set value / active on the delegate when it carries them. A
        // delegate that lacks a property simply ignores that input
        // (volume/brightness use value; mic/caps use active).
        function apply(item, value, active) {
            if (value !== undefined && item.value !== undefined)
                item.value = value;
            if (active !== undefined && item.active !== undefined)
                item.active = active;
        }

        function destroyDelegate() {
            if (priv.delegate) {
                // Defer the destroy out of the current call. show() can be
                // invoked synchronously from C++ (the IPC router dispatches
                // osd.show via the meta-object system); destroying a QML
                // object inline inside such a call re-enters the engine and
                // corrupts the invoked function's return-value marshalling
                // (the caller sees a default-constructed result). Qt.callLater
                // runs the teardown after the current call unwinds, which is
                // also the right place to destroy an object rather than
                // mid-handler.
                const old = priv.delegate;
                priv.delegate = null;
                Qt.callLater(function () {
                    if (!old)
                        return;
                    // The provider must hand back a destroyable (JS-owned)
                    // delegate, since the host owns its lifetime. A
                    // C++-owned item throws "indestructible object" here;
                    // catch it so a provider wiring bug surfaces as a clear
                    // warning instead of an uncaught delayed-eval exception
                    // (and so it doesn't silently leak a ghost behind the
                    // next OSD).
                    try {
                        old.destroy();
                    } catch (e) {
                        console.warn("OSDHost: delegate is not destroyable; the provider must return a JS-owned item (QQmlEngine::JavaScriptOwnership). Leaking it.", e);
                    }
                });
            }
            priv.currentKind = "";
        }

        // Called at the end of the hide transition: tear the delegate
        // down and announce the kind that just left.
        function onHidden() {
            const k = priv.currentKind;
            priv.destroyDelegate();
            if (k !== "")
                root.hidden(k);
        }
    }

    // The OSD card holder: positioned bottom-centre, sized to the
    // delegate, and the thing the show/hide transitions animate.
    Item {
        id: frame

        anchors.horizontalCenter: parent.horizontalCenter
        y: parent.height - height - root.bottomMargin
        implicitWidth: childrenRect.width
        implicitHeight: childrenRect.height
        width: implicitWidth
        height: implicitHeight
        opacity: 0
        scale: 0.92
        visible: opacity > 0
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
            PropertyChanges {
                target: frame
                opacity: 1
                scale: 1
            }
        },
        State {
            name: "hidden"
            PropertyChanges {
                target: frame
                opacity: 0
                scale: 0.92
            }
        }
    ]
    transitions: [
        Transition {
            to: "shown"
            NumberAnimation {
                target: frame
                properties: "opacity,scale"
                duration: Motion.duration_short_4
                easing: Motion.emphasized
            }
        },
        Transition {
            to: "hidden"
            SequentialAnimation {
                NumberAnimation {
                    target: frame
                    properties: "opacity,scale"
                    duration: Motion.duration_short_3
                    easing: Motion.standard
                }
                ScriptAction {
                    script: priv.onHidden()
                }
            }
        }
    ]
}
