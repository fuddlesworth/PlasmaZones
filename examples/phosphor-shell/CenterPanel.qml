// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

PanelWindow {
    id: root

    // Calendar popup anchors to the clock text item.
    property alias calendarAnchor: clockLabel
    required property var shellState
    required property string clockText

    edge: PanelWindow.Top
    thickness: 38
    alignment: PanelWindow.Center
    panelLength: 0 // auto-fit to content
    implicitWidth: clockLabel.implicitWidth

    margins {
        top: 6
    }

    ShaderBackground {
        anchors.fill: parent
        playing: true
        shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
        shaderParams: {
            "speed": 1.2,
            "angle": 0,
            "cornerRadius": 12
        }
        // Catppuccin mocha mauve → macchiato sky — visibly distinct
        // gradient endpoints so the rotation is actually legible.
        customColor1: "#cba6f7"
        customColor2: "#89dceb"
    }

    Text {
        id: clockLabel

        anchors.centerIn: parent
        text: root.clockText || "..."
        color: "#1e1e2e"
        font.pixelSize: 13
        font.weight: Font.Bold
        leftPadding: 20
        rightPadding: 20
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.shellState.calendarOpen = !root.shellState.calendarOpen
    }

}
