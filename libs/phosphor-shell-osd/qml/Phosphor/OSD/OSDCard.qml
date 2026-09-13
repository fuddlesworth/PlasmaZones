// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
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
    property int edge: OSDCard.Bottom
    property real reveal: 1
    property Component decoration: null
    readonly property bool _vertical: edge === OSDCard.Right
    readonly property real _fraction: Math.max(0, Math.min(1, progress))
    readonly property real _length: _vertical ? height : width
    readonly property real _fill: showProgress ? _fraction : 1
    implicitWidth: _vertical ? 40 : 400
    implicitHeight: _vertical ? 400 : 40
    Accessible.role: Accessible.AlertMessage
    Accessible.name: showProgress ? qsTr("%1, %2 percent").arg(label).arg(Math.round(_fraction * 100)) : label

    Rectangle {
        id: band
        property bool shaderAnchor: true
        property real _len: card._length * card._fill * card.reveal
        x: card._vertical ? card.width - 5 : 0
        y: card._vertical ? card.height - _len : card.height - 5
        width: card._vertical ? 5 : _len
        height: card._vertical ? _len : 5
        gradient: Gradient {
            orientation: card._vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop {
                position: 0
                color: Appearance.stops[0]
            }
            GradientStop {
                position: 0.34
                color: Appearance.stops[1]
            }
            GradientStop {
                position: 0.68
                color: Appearance.stops[2]
            }
            GradientStop {
                position: 1
                color: Appearance.stops[3]
            }
        }
        Behavior on _len {
            SettleAnimation {}
        }
    }
    DecorationSlot {
        anchors.fill: parent
        component: card.decoration
        contentItem: band
        surfacePath: "shell.phosphor.osd"
        focused: card.reveal > 0
    }
    ShellSurface {
        id: bubble
        width: readout.implicitWidth + 18
        height: readout.implicitHeight + 12
        radius: 6
        shadowed: false
        x: card._vertical ? card.width - width - 11 : Math.max(0, band._len - width)
        y: card._vertical ? Math.max(0, card.height - band._len) : card.height - 11 - height
        opacity: card.reveal
        Text {
            id: readout
            anchors.centerIn: parent
            text: card.showProgress ? qsTr("%1 %2%").arg(card.label).arg(Math.round(card._fraction * 100)) : card.label
            color: Appearance.text
            font.family: Tokens.font_family_mono
            font.pixelSize: 10
        }
    }
}
