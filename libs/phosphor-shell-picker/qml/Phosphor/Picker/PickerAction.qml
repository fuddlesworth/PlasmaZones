// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Picker.PickerAction, a text action with a 2 px underline.
//
// The picker's Apply and Apply on all screens (A3 §8): no filled button,
// the word itself in the ramp colour with the underline that IS the
// control (05 R2). `primary` lights the word blue and rests the underline
// visible; a secondary action shows its underline on hover only. The
// underline ticks on activation.
//
//   PickerAction { text: qsTr("Apply"); primary: true; onActivated: ... }

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: action

    property string text: ""
    property bool primary: false
    // Rail position of the underline's hue; blue for the primary action.
    property real t: primary ? 0.33 : 0

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
        color: action.primary ? Spectrum.active : (action.hovered ? Theme.on_surface : Theme.on_surface_variant)
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
