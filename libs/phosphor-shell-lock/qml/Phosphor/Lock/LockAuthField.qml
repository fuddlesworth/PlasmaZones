// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Lock.LockAuthField, the 320 x 44 password field.
//
// Abyss ground, the field radius, a 1 px edge on the state axis: cyan
// idle, blue while authenticating, and a rose pulse that returns to cyan
// on a wrong password (A3 §6 c, e). The field never shakes; the failure
// reads as the pulse plus the reason text beneath. It draws no caret of
// its own input: the dots come from the shared LockController, typed one
// by one, so every screen shows the same entry.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme

Rectangle {
    id: root

    property var controller: null

    readonly property string phase: root.controller ? root.controller.phase : "idle"
    readonly property int dotCount: root.controller ? root.controller.password.length : 0
    readonly property string placeholder: root.controller && root.controller.placeholder.length > 0 ? root.controller.placeholder : qsTr("Password")
    readonly property string errorText: root.controller ? root.controller.errorText : ""

    // Abyss `#070F22` (A3 consistency table, the floating-card ground).
    readonly property color groundColor: "#070F22"

    // Pinned for the same reason as the ground: this field sits on the lock's
    // fixed dark field, so palette-driven ink would go dark-on-dark under a
    // light palette. Values are the dark palette's on_surface / variant.
    readonly property color inkColor: "#E6EDFF"
    readonly property color inkMutedColor: "#94A3B8"

    implicitWidth: 320
    implicitHeight: 44
    radius: Tokens.radius_edge
    color: root.groundColor
    border.width: 1
    border.color: root.phase === "authenticating" ? Spectrum.active : pulse.edge

    Behavior on border.color {
        ColorAnimation {
            duration: Motion.duration_enter_content
            easing: Motion.reveal
        }
    }

    Accessible.role: Accessible.EditableText
    Accessible.name: qsTr("Password")
    Accessible.description: root.errorText

    // The wrong-password pulse: rose, then back to cyan on the release
    // envelope.
    QtObject {
        id: pulse

        property color edge: Spectrum.resting
    }

    SequentialAnimation {
        id: errorPulse

        ColorAnimation {
            target: pulse
            property: "edge"
            to: Spectrum.hot
            duration: Motion.duration_enter
            easing: Motion.reveal
        }
        ColorAnimation {
            target: pulse
            property: "edge"
            to: Spectrum.resting
            duration: Motion.duration_release
            easing: Motion.release
        }
    }

    Connections {
        target: root.controller
        ignoreUnknownSignals: true

        function onFailed(): void {
            errorPulse.restart();
        }
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Tokens.spacing_m
        anchors.right: glyph.left
        anchors.rightMargin: Tokens.spacing_s
        anchors.verticalCenter: parent.verticalCenter
        visible: root.dotCount === 0
        text: root.placeholder
        elide: Text.ElideRight
        color: root.inkMutedColor
        font.family: Tokens.font_family_ui
        font.pixelSize: Tokens.font_size_body_m
        opacity: 0.7
    }

    Row {
        id: dots

        anchors.left: parent.left
        anchors.leftMargin: Tokens.spacing_m
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.spacing_xs

        Repeater {
            model: root.dotCount

            delegate: Rectangle {
                width: 6
                height: 6
                radius: 3
                color: root.inkColor
                // Each dot lands as it is typed.
                opacity: 0
                Component.onCompleted: opacity = 1
                Behavior on opacity {
                    NumberAnimation {
                        duration: Motion.duration_enter
                        easing: Motion.reveal
                    }
                }
            }
        }
    }

    // The caret sits after the dots while the field is idle.
    Rectangle {
        anchors.left: dots.right
        anchors.leftMargin: root.dotCount > 0 ? Tokens.spacing_xs : 0
        anchors.verticalCenter: parent.verticalCenter
        width: 1
        height: 18
        color: root.inkColor
        visible: root.dotCount > 0 && root.phase === "idle"
        SequentialAnimation on opacity {
            running: root.dotCount > 0 && root.phase === "idle"
            loops: Animation.Infinite
            NumberAnimation {
                to: 0
                duration: Motion.duration_long_2
            }
            NumberAnimation {
                to: 1
                duration: Motion.duration_long_2
            }
        }
    }

    Kirigami.Icon {
        id: glyph

        anchors.right: parent.right
        anchors.rightMargin: Tokens.spacing_m
        anchors.verticalCenter: parent.verticalCenter
        width: 16
        height: 16
        source: "object-locked"
        isMask: true
        color: root.border.color
        opacity: 0.6
    }

    Text {
        anchors.left: parent.left
        anchors.top: parent.bottom
        anchors.topMargin: Tokens.spacing_s
        width: parent.width
        visible: root.errorText.length > 0
        text: root.errorText
        wrapMode: Text.WordWrap
        color: Spectrum.hot
        font.family: Tokens.font_family_ui
        font.pixelSize: Tokens.font_size_body_m
    }
}
