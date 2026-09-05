// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.OSD.OSDCard, shared chrome for the built-in OSDs: an edge band.
//
// An OSD is a value drawn on the edge that concerns it (A3 §4), not a
// centred card. The band is 3 px along one screen edge, its length the
// value and its colour the state-axis sample of that value (cyan low,
// rose at the limit); the glyph and the tabular readout ride the fill
// point. Stateful OSDs (mic, caps) draw a 2 px band across the whole
// edge with the glyph and label at its centre.
//
// The card only draws along whatever frame OSDHost gives it: the host
// places a bottom or top band on the FOCUSED WINDOW's edge from the
// placement map, and on the screen edge when no window is focused.
//
//   OSDCard {
//       edge: OSDCard.Bottom
//       label: qsTr("Volume")
//       showProgress: true
//       progress: value / 100
//       icon: Component { /* glyph */ }
//   }

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: card

    enum Edge {
        Bottom,
        Top,
        Right
    }

    property Component icon: null
    property string label: ""
    property bool showProgress: false
    property real progress: 0
    // Which screen edge the band lives on. OSDHost reads it to place the
    // frame.
    property int edge: OSDCard.Bottom
    // How much of the band is drawn, 0..1; OSDHost animates it for the
    // enter and retract.
    property real reveal: 1

    readonly property bool _vertical: card.edge === OSDCard.Right
    readonly property real _fraction: Math.max(0, Math.min(1, card.progress))
    readonly property real _length: card._vertical ? card.height : card.width
    readonly property real _fill: card.showProgress ? card._fraction : 1
    readonly property color _hue: card.showProgress ? Spectrum.at(card._fraction) : Spectrum.resting

    // Sized by the host to the edge it spans; the band is thin.
    implicitWidth: card._vertical ? 40 : 400
    implicitHeight: card._vertical ? 400 : 40

    Accessible.role: Accessible.AlertMessage
    Accessible.name: card.showProgress ? qsTr("%1, %2 percent").arg(card.label).arg(Math.round(card._fraction * 100)) : card.label

    // Track: the whole edge at rest opacity.
    Rectangle {
        visible: card.showProgress
        x: card._vertical ? card.width - 3 : 0
        y: card._vertical ? 0 : card.height - 3
        width: card._vertical ? 3 : card.width
        height: card._vertical ? card.height : 3
        color: Theme.on_surface
        opacity: 0.12 * card.reveal
    }

    // The band. Grows from its source point (the edge's start) to the
    // value; `reveal` scales it for enter/retract.
    Rectangle {
        id: band

        property real _len: card._length * card._fill * card.reveal
        x: card._vertical ? card.width - 3 : 0
        y: card._vertical ? card.height - _len : card.height - (card.showProgress ? 3 : 2)
        width: card._vertical ? 3 : _len
        height: card._vertical ? _len : (card.showProgress ? 3 : 2)
        color: card._hue

        Behavior on _len {
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
    }

    // Glyph + readout riding the fill point.
    Row {
        id: readout

        spacing: Tokens.spacing_s
        opacity: card.reveal
        x: card._vertical ? card.width - width - 8 : Math.max(0, Math.min(card.width - width, band._len - width / 2))
        y: card._vertical ? Math.max(0, Math.min(card.height - height, card.height - band._len - height / 2)) : card.height - 3 - height - 6

        Loader {
            anchors.verticalCenter: parent.verticalCenter
            sourceComponent: card.icon
        }

        TabularText {
            anchors.verticalCenter: parent.verticalCenter
            visible: card.showProgress
            text: Math.round(card._fraction * 100)
            font.pixelSize: Tokens.font_size_display_m
            font.weight: Tokens.font_weight_medium
            tickOnChange: true
            t: card._fraction
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: !card.showProgress
            Accessible.ignored: true
            text: card.label
            color: Theme.on_surface_variant
            font.family: Tokens.font_family_ui
            font.pixelSize: Tokens.font_size_label_m
            font.capitalization: Font.AllUppercase
            font.letterSpacing: 1
        }
    }
}
