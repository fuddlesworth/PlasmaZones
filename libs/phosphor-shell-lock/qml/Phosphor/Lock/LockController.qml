// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Lock.LockController, the one auth field behind every screen.
//
// The lock screen runs one LockScreen per output, but there is only one
// password being typed. This object holds it: every screen's field draws
// the same dots, and whichever screen the compositor gave keyboard focus
// feeds keys in here (A3 §6 d: typing goes to the field without clicking
// it). Submit goes to `lock.unlock(password)`; the field's phase follows
// `lock.state` and the failure signal.
//
// `lock` is a LockService, or anything with its surface: `state` (0
// Unlocked, 1 Locking, 2 Locked, 3 Authenticating), `locked`,
// `unlock(password)`, and the `stateChanged` / `authenticationFailed(reason)`
// / `unlocked` signals. Duck-typed so a test can drive it with a fake.
//
// Unlock: the service releases the compositor lock the moment PAM says yes,
// so the real windows are back before any animation could run. The
// content still dismisses over `Motion.duration_dismiss` behind
// `dismissing`, and `surfacesWanted` keeps the surfaces up for that long,
// so a compositor that holds the last lock frame shows the outlines fill.

import QtQml
import Phosphor.Theme

QtObject {
    id: controller

    property var lock: null

    readonly property int stateUnlocked: 0
    readonly property int stateLocking: 1
    readonly property int stateLocked: 2
    readonly property int stateAuthenticating: 3

    // The password so far. Cleared on failure, on Escape, and after the
    // dismiss; never logged.
    property string password: ""
    // Shown in the empty field. Clicking an outline sets it to that
    // window's name (`Unlock to return to Firefox`).
    property string placeholder: ""
    // The last failure's reason, cleared by the next key.
    property string errorText: ""

    readonly property bool locked: controller.lock ? !!controller.lock.locked : false
    readonly property bool authenticating: controller.lock ? controller.lock.state === controller.stateAuthenticating : false
    // True for Motion.duration_dismiss after a successful unlock.
    property bool dismissing: false
    // "idle", "authenticating", "error" or "dismissing": what the field's
    // edge shows (A3 §6 e).
    readonly property string phase: controller.dismissing ? "dismissing" : (controller.authenticating ? "authenticating" : (controller.errorText !== "" ? "error" : "idle"))
    // Whether the lock surfaces should exist: while locked, and through
    // the dismiss.
    readonly property bool surfacesWanted: controller.locked || controller.dismissing

    // An unlock attempt was rejected; the field pulses its edge on this.
    signal failed(string reason)
    // The dismiss finished; the surfaces can go.
    signal dismissed

    // Feed a key event. Returns true when the key was consumed.
    function handleKey(event: var): bool {
        if (!controller.locked || controller.dismissing)
            return false;
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            controller.submit();
            return true;
        }
        if (event.key === Qt.Key_Escape) {
            controller.clear();
            return true;
        }
        if (controller.authenticating)
            return false;
        if (event.key === Qt.Key_Backspace) {
            controller.errorText = "";
            controller.password = controller.password.slice(0, -1);
            return true;
        }
        // Printable text only: modifiers other than Shift, and control
        // characters, are not password input.
        if (event.modifiers & ~(Qt.ShiftModifier | Qt.KeypadModifier))
            return false;
        const text = event.text;
        if (!text || text.length === 0 || text.charCodeAt(0) < 0x20)
            return false;
        controller.errorText = "";
        controller.password += text;
        return true;
    }

    function submit(): void {
        if (!controller.lock || !controller.locked || controller.authenticating || controller.password === "")
            return;
        controller.errorText = "";
        controller.lock.unlock(controller.password);
    }

    function clear(): void {
        controller.password = "";
        controller.errorText = "";
    }

    function placeholderFor(name: string): void {
        controller.placeholder = name && name.length > 0 ? qsTr("Unlock to return to %1").arg(name) : "";
    }

    property Connections _lockConnections: Connections {
        target: controller.lock
        ignoreUnknownSignals: true

        function onAuthenticationFailed(reason: string): void {
            controller.password = "";
            controller.errorText = reason && reason.length > 0 ? reason : qsTr("That password was not accepted");
            controller.failed(controller.errorText);
        }

        // Authentication succeeded and the compositor lock is still held:
        // run the exit while the surfaces are on screen, then let go.
        function onAboutToUnlock(): void {
            controller.errorText = "";
            controller.dismissing = true;
            controller._dismissTimer.restart();
        }

        // A service without the release handshake (or the failsafe having
        // fired first) lands here; nothing to hold any more.
        function onUnlocked(): void {
            if (controller.dismissing)
                return;
            controller.errorText = "";
            controller.dismissing = true;
            controller._dismissTimer.restart();
        }

        function onStateChanged(): void {
            // A fresh lock starts with an empty field and no stale
            // placeholder from the last session.
            if (controller.lock && controller.lock.state === controller.stateLocked && controller.password === "")
                controller.placeholder = "";
        }
    }

    property Timer _dismissTimer: Timer {
        // The exit is two animations in parallel: the outlines fill over
        // duration_release and the content block fades over duration_dismiss.
        // Hold for the LONGER of them — releasing at duration_dismiss drops
        // the surfaces while the fill is barely a third done, so the exit the
        // header describes never actually plays. Still far inside the state
        // machine's 1 s release failsafe.
        interval: Math.max(Motion.duration_release, Motion.duration_dismiss)
        repeat: false
        onTriggered: {
            // The exit has played: release the compositor lock now.
            if (controller.lock && typeof controller.lock.finishUnlock === "function")
                controller.lock.finishUnlock();
            controller.dismissing = false;
            controller.password = "";
            controller.placeholder = "";
            controller.dismissed();
        }
    }
}
