// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * Auto-dismiss timer + idempotency latch shared by every OSD content type
 * loaded into NotificationOverlay.qml's mode-driven Loader.
 *
 * Why this is its own component: the timer-fire path and the click
 * MouseArea path can both attempt to dismiss within the same show cycle.
 * Without the latch, C++ runs `Surface::hide()` twice and the second call
 * qCWarnings on the already-Hidden state. The latch resets on every
 * `restart()` via the Connections block — that way any path that restarts
 * the timer keeps the latch in sync without the helper having to remember
 * to clear it.
 *
 * Sibling latch — LayoutPickerContent.qml's `_dismissed` property + its
 * private `_requestDismiss()`: same idempotency idea, but its dismiss
 * source is user actions (Escape, backdrop click) rather than a timer.
 * The reset is driven by an explicit C++ property write on every show,
 * not by a `runningChanged` transition. The two latches are deliberately
 * separate components — they share a contract (at most one dismiss per
 * show cycle) but the trigger surface and reset mechanism differ enough
 * that pulling them into a common base would obscure both. If a third
 * dismissal-style content type ever appears, revisit factoring out a
 * shared `DismissLatch { signal request(); function fire(); function reset() }`.
 *
 * Usage from a content Item (e.g. LayoutOsdContent.qml):
 *
 *     OsdDismissable {
 *         id: dismiss
 *         interval: root.displayDuration
 *         onRequest: root.dismissRequested()
 *     }
 *     // host calls root.restartDismissTimer() — forward:
 *     function restartDismissTimer() { dismiss.restart() }
 *     // click-to-dismiss MouseArea:
 *     onClicked: dismiss.fire()
 */
Item {
    id: helper

    /// Auto-dismiss interval. Fade shapes are owned by the SurfaceAnimator's
    /// `osd.show` / `osd.pop` / `osd.hide` profiles; tune the JSONs for
    /// appear/disappear feel rather than re-introducing per-window duration
    /// overrides on top of this.
    property int interval: 1000
    /// Internal: at most-once latch within a show cycle. Reset by the
    /// Connections block when the timer (re)starts.
    property bool _dismissed: false

    /// Emitted at most once per show cycle (timer fire OR explicit
    /// `fire()` call). Forward to the content's own public
    /// `dismissRequested` signal — the unified NotificationOverlay host
    /// re-emits that as its own `dismissRequested` so
    /// OverlayService::createWarmedOsdSurface's connect to Surface::hide()
    /// drives the library animator's beginHide.
    signal request()

    /// Restart the auto-dismiss timer. Resets the idempotency latch via
    /// the dismissTimer.runningChanged Connections block below.
    function restart() {
        dismissTimer.restart();
    }

    /// Explicit dismiss request from a click MouseArea (or any other
    /// caller). Idempotent within a single show cycle.
    function fire() {
        if (helper._dismissed)
            return ;

        helper._dismissed = true;
        helper.request();
    }

    Timer {
        id: dismissTimer

        interval: helper.interval
        onTriggered: helper.fire()
    }

    Connections {
        function onRunningChanged() {
            if (dismissTimer.running)
                helper._dismissed = false;

        }

        target: dismissTimer
    }

}
