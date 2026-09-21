// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.ChordPill, the cheatsheet's one pill (A3 §10 b).
//
// 20 px tall, `radius_edge` corners, abyss ground at 90 % under a 1 px
// cyan edge, the chord in 12 px tabular text. An unbound chord reads
// "unbound" and dims; a `hot` pill (the chord just performed, or the
// hovered one) turns its edge white. `description` shows under the pill
// while hovered.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property string chord: ""
    property string label: ""
    property string description: ""
    property bool assigned: true
    property bool hot: false

    readonly property bool hovered: hover.hovered
    readonly property string _text: assigned ? chord : qsTr("unbound")

    implicitWidth: pill.width
    implicitHeight: pill.height
    width: implicitWidth
    height: implicitHeight
    opacity: assigned ? 1 : 0.45

    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + _text

    Rectangle {
        id: pill

        width: text.contentWidth + Tokens.spacing_s * 2
        height: 20
        radius: Tokens.radius_edge
        color: Qt.rgba(Theme.surface.r, Theme.surface.g, Theme.surface.b, 0.9)
        border.width: 1
        border.color: root.hot || root.hovered ? Spectrum.focus : Spectrum.resting

        Behavior on border.color {
            ColorAnimation {
                duration: Motion.duration_enter
                easing: Motion.enter
            }
        }

        TabularText {
            id: text

            anchors.centerIn: parent
            text: root._text
            font.pixelSize: Tokens.font_size_label_m
            color: Theme.on_surface
        }
    }

    // The description, 13 px under the pill while hovered.
    Text {
        anchors.top: pill.bottom
        anchors.topMargin: Tokens.spacing_xxs
        anchors.horizontalCenter: pill.horizontalCenter
        visible: root.hovered && text !== ""
        text: root.description !== "" ? root.description : root.label
        color: Theme.on_surface_variant
        font.family: Tokens.font_family_ui
        font.pixelSize: Tokens.font_size_body_m
    }

    HoverHandler {
        id: hover
    }
}
