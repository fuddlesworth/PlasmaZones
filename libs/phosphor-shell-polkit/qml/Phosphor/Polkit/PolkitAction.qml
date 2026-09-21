// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Polkit.PolkitAction, a text action with a 2 px underline.
//
// Cancel and Authenticate on the prompt (A3 §9b): the word in rose, the
// prompt's colour throughout, over the underline that IS the control (05
// R2). `primary` rests the underline visible; the secondary action shows
// it on hover. The underline ticks on activation.
//
//   PolkitAction { text: qsTr("Authenticate"); primary: true; onActivated: ... }

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: action

    property string text: ""
    property bool primary: false
    // Rose: the prompt is an interruption that grants power (A3 §9e).
    property real t: 1

    signal activated

    readonly property bool hovered: hover.hovered

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight + Tokens.spacing_xxs + 2

    Accessible.role: Accessible.Button
    Accessible.name: action.text
    Accessible.onPressAction: action.activated()

    Text {
        id: label

        anchors.left: parent.left
        anchors.top: parent.top
        text: action.text
        color: action.primary ? Spectrum.hot : (action.hovered ? Theme.on_surface : Theme.on_surface_variant)
        font.family: Tokens.font_family_ui
        font.pixelSize: Tokens.font_size_body_l
        font.weight: action.primary ? Tokens.font_weight_demibold : Tokens.font_weight_medium

        Behavior on color {
            ColorAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
    }

    SpectrumUnderline {
        id: underline

        anchors.left: parent.left
        anchors.top: label.bottom
        anchors.topMargin: Tokens.spacing_xxs
        length: label.contentWidth
        t: action.t
        restOpacity: action.primary || action.hovered ? 1 : 0
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: {
            underline.tick();
            action.activated();
        }
    }
}
