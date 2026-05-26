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

    /// Display `msg` immediately, replacing any in-flight toast.
    function show(msg) {
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
    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.85)
    opacity: 0
    visible: opacity > 0
    z: 100

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

        PauseAnimation {
            duration: 2000
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
