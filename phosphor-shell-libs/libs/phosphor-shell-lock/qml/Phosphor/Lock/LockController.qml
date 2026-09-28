// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// One password and authentication lifecycle shared by every output. The real
// service keeps the compositor lock held during the exit, then finishUnlock()
// releases it. Rendering and input never decide whether authentication passed.

import QtQml
import Phosphor.Theme

QtObject {
    id: controller

    property var lock: null

    readonly property int stateUnlocked: 0
    readonly property int stateLocking: 1
    readonly property int stateLocked: 2
    readonly property int stateAuthenticating: 3
    // The surfaces are still up playing their exit while the service releases
    // the compositor lock. `locked` is still true here.
    readonly property int stateReleasing: 4

    // The password so far. Cleared on failure, on Escape, and after the
    // dismiss; never logged.
    property string password: ""
    // The last failure's reason, cleared by the next key.
    property string errorText: ""

    readonly property bool locked: controller.lock ? !!controller.lock.locked : false
    readonly property bool authenticating: controller.lock ? controller.lock.state === controller.stateAuthenticating : false
    // True for Motion.duration_dismiss after a successful unlock.
    property bool dismissing: false
    readonly property int dismissDuration: Appearance.motion ? Motion.duration_long_3 : 0
    // "idle", "authenticating", "error" or "dismissing": what the field's
    // edge shows (A3 §6 e).
    readonly property string phase: controller.dismissing ? "dismissing" : (controller.authenticating ? "authenticating" : (controller.errorText !== "" ? "error" : "idle"))
    // True from the moment a lock object exists, which is not the same as
    // `locked`. ext-session-lock expects the surfaces to be created against
    // the lock right after it is requested; a compositor that withholds
    // `locked` until every output has one would never send it if we waited
    // for it first. So the surfaces follow the request, not the grant.
    readonly property bool lockPending: controller.lock ? controller.lock.state !== controller.stateUnlocked : false
    // Whether the lock surfaces should exist: from the request, through the
    // lock, and on through the dismiss.
    readonly property bool surfacesWanted: controller.lockPending || controller.dismissing

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
            const length = controller.password.length;
            const tail = controller.password.charCodeAt(length - 1);
            controller.password = controller.password.slice(0, length - (tail >= 0xDC00 && tail <= 0xDFFF ? 2 : 1));
            return true;
        }
        // Printable text only. Command chords are rejected by their modifier,
        // everything else by not producing a printable character.
        //
        // AltGr MUST get through. It is how a great many layouts reach
        // characters that appear in real passwords (@ and € on a German
        // layout, the whole accented row on a Polish one), and Qt spells it
        // either as GroupSwitchModifier or, on some platforms, as Control+Alt
        // together. Filtering on "anything but Shift" dropped every one of
        // those keystrokes silently, which for those users is not a rejected
        // character, it is a lock screen their password cannot be typed into.
        const mods = event.modifiers;
        const altGr = (mods & Qt.GroupSwitchModifier) || ((mods & Qt.ControlModifier) && (mods & Qt.AltModifier));
        if (!altGr && (mods & (Qt.ControlModifier | Qt.MetaModifier)))
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
            if (controller.lock && controller.lock.state === controller.stateLocking)
                controller.clear();
        }
    }

    property Timer _dismissTimer: Timer {
        interval: controller.dismissDuration
        repeat: false
        onTriggered: {
            // The exit has played: release the compositor lock now.
            if (controller.lock && typeof controller.lock.finishUnlock === "function")
                controller.lock.finishUnlock();
            controller.dismissing = false;
            controller.password = "";
            controller.dismissed();
        }
    }
}
