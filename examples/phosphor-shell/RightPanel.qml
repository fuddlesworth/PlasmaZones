// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

PanelWindow {
    id: root

    required property var shellState
    required property string cpuPercent
    required property string memPercent
    required property string batteryPercent
    required property bool batteryVisible

    edge: PanelWindow.Top
    thickness: 38
    alignment: PanelWindow.End
    panelLength: 0 // auto-fit to content
    implicitWidth: rightRow.implicitWidth

    margins {
        right: 8
        top: 6
    }

    ShaderBackground {
        anchors.fill: parent
        playing: root.visible
        shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
        shaderParams: {
            "tintOpacity": 0.85,
            "noiseAmount": 0.18,
            "noiseScale": 18,
            "animSpeed": 0.6,
            "cornerRadius": 12
        }
        customColor1: "#1e1e2e"
    }

    Row {
        id: rightRow

        anchors.centerIn: parent
        height: parent.height
        spacing: 14
        leftPadding: 12
        rightPadding: 12

        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: "CPU"
                color: "#a6e3a1"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                text: (root.cpuPercent || "0") + "%"
                color: "#cdd6f4"
                font.pixelSize: 11
            }

        }

        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: "MEM"
                color: "#89dceb"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                text: (root.memPercent || "0") + "%"
                color: "#cdd6f4"
                font.pixelSize: 11
            }

        }

        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter
            // Both gates: the file must exist AND the read must have
            // produced a value. FileView.exists can flicker true during
            // cold-start before the read completes; without the length
            // check the row would briefly render a bare "%" sign.
            visible: root.batteryVisible && root.batteryPercent.length > 0

            Text {
                text: "BAT"
                color: "#f9e2af"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                text: root.batteryPercent + "%"
                color: "#cdd6f4"
                font.pixelSize: 11
            }

        }

        Rectangle {
            width: 26
            height: 26
            anchors.verticalCenter: parent.verticalCenter
            radius: 6
            color: settingsArea.containsMouse ? "#45475a" : "transparent"

            Text {
                anchors.centerIn: parent
                text: "⚙"
                color: "#cdd6f4"
                font.pixelSize: 13
            }

            MouseArea {
                id: settingsArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: root.shellState.settingsOpen ? "Close settings" : "Open settings"
                onClicked: root.shellState.settingsOpen = !root.shellState.settingsOpen
            }

        }

    }

}
