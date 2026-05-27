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
        toastShow.restart();
        toastHide.restart();
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
    Accessible.role: Accessible.StaticText
    Accessible.name: root.message
    Accessible.description: i18n("Toast notification")

    Label {
        id: toastLabel

        anchors.centerIn: parent
        text: root.message
        color: Kirigami.Theme.backgroundColor
        font.weight: Font.Medium
    }

    PhosphorMotionAnimation {
        id: toastShow

        target: root
        properties: "opacity"
        from: 0
        to: 1
        profile: "popup"
        durationOverride: 200
    }

    SequentialAnimation {
        id: toastHide

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
