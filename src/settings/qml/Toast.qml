// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * Bottom-of-window toast notification.
 *
 * Pill-shaped surface that fades in via the `popup` motion profile,
 * holds for 2 seconds, then fades out via `widget.fadeOut`. The
 * `show(msg)` invocable swaps the label and restarts both animations
 * so back-to-back calls override the previous toast cleanly.
 *
 * Anchored relative to whatever `parent` is assigned at instantiation
 * time — typical wiring is `parent: window.contentItem` so the toast
 * floats above the chrome regardless of the active page.
 */
Rectangle {
    id: root

    /// Last message passed to show(). Read-only externally — write via show().
    property string message: ""
    // Toast surface tint — extracted to a single readonly so future
    // theme tweaks live in one place (E32 follow-up).
    readonly property color toastBg: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)

    /// Display `msg` immediately, replacing any in-flight toast.
    function show(msg: string) {
        root.message = msg;
        // AT-SPI Notification announcements — Orca / other screen
        // readers don't announce StaticText surfaces that aren't
        // focused, so use Accessible.announcement to push the toast
        // text into the AT consumer's speech queue regardless of
        // focus state. Pairs with the Accessible.role: Notification
        // below (Qt 6.6+).
        root.Accessible.announcement = msg;
        // Single SequentialAnimation — back-to-back show() calls
        // restart() the same animation cleanly, vs. the previous
        // shape where two concurrent animations could overlap if a
        // new show() arrived mid-fade-out (the fade-in started while
        // the still-running fade-out was driving opacity toward 0).
        toastAnimation.restart();
    }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Kirigami.Units.largeSpacing * 4
    width: toastLabel.implicitWidth + Kirigami.Units.largeSpacing * 3
    height: toastLabel.implicitHeight + Kirigami.Units.largeSpacing * 1.5
    radius: height / 2
    color: root.toastBg
    opacity: 0
    visible: opacity > 0
    z: 100
    // Toast is a status-message surface — announce to AT consumers.
    // Accessible.Notification (Qt 6.6+) is announced by Orca even
    // when the toast doesn't have focus; StaticText was silent for
    // screen-reader users because the toast never receives focus.
    Accessible.role: Accessible.Notification
    Accessible.name: root.message
    Accessible.description: i18n("Toast notification")

    Label {
        id: toastLabel

        anchors.centerIn: parent
        text: root.message
        color: Kirigami.Theme.backgroundColor
        font.weight: Font.Medium
    }

    SequentialAnimation {
        id: toastAnimation

        // Fade-in → hold → fade-out as a single sequence so back-to-back
        // show() calls interrupt cleanly via restart(). The prior shape
        // ran two concurrent animations (toastShow + toastHide), which
        // could overlap if a new show() arrived mid-fade-out — the new
        // fade-in then started while the still-running fade-out was
        // driving opacity toward 0.
        PhosphorMotionAnimation {
            target: root
            properties: "opacity"
            from: 0
            to: 1
            profile: "popup"
            // Scales with the user's animation-speed preference /
            // respects "reduce motion" — was a hardcoded 200ms that
            // bypassed both.
            durationOverride: Kirigami.Units.shortDuration
        }

        // Kirigami.Units.veryLongDuration (≈400ms) × 5 keeps the toast
        // legible (≈2s on stock themes) while respecting the user's
        // global animation-speed scale — hardcoding 2000ms ignored a
        // user who'd configured the desktop to faster/slower motion.
        PauseAnimation {
            duration: Kirigami.Units.veryLongDuration * 5
        }

        PhosphorMotionAnimation {
            target: root
            properties: "opacity"
            from: 1
            to: 0
            profile: "widget.fadeOut"
        }
    }
}
