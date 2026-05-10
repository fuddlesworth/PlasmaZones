// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

PanelWindow {
    id: root

    // Anchor item for the menu popup — exposed so shell.qml can wire the
    // popup to it without reaching into this component's internals.
    property alias menuAnchor: menuButton
    required property var shellState

    edge: PanelWindow.Top
    thickness: 38
    alignment: PanelWindow.Start
    panelLength: 0 // auto-fit to content
    implicitWidth: leftRow.implicitWidth

    margins {
        left: 8
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
        id: leftRow

        anchors.centerIn: parent
        height: parent.height
        spacing: 12
        leftPadding: 12
        rightPadding: 12

        Rectangle {
            id: menuButton

            width: 30
            height: 30
            anchors.verticalCenter: parent.verticalCenter
            radius: 8
            color: menuArea.containsMouse ? "#45475a" : "transparent"

            Text {
                anchors.centerIn: parent
                text: root.shellState.menuOpen ? "✕" : "☰"
                color: root.shellState.menuOpen ? "#f38ba8" : "#cdd6f4"
                font.pixelSize: 14
            }

            MouseArea {
                id: menuArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                Accessible.role: Accessible.Button
                Accessible.name: root.shellState.menuOpen ? "Close menu" : "Open menu"
                onClicked: root.shellState.menuOpen = !root.shellState.menuOpen
            }

        }

        Row {
            spacing: 6
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: 5

                // NOTE: this example uses indices for "workspaces" which is
                // pedagogical only — production shells should bind to
                // compositor-provided workspace IDs (per the project's
                // "Zone IDs, never indices" rule).
                Rectangle {
                    required property int index

                    width: index === root.shellState.activeWorkspace ? 20 : 8
                    height: 8
                    radius: 4
                    color: index === root.shellState.activeWorkspace ? "#89b4fa" : "#45475a"

                    MouseArea {
                        anchors.fill: parent
                        Accessible.role: Accessible.Button
                        Accessible.name: "Switch to workspace " + (parent.index + 1)
                        onClicked: root.shellState.activeWorkspace = parent.index
                    }

                    Behavior on width {
                        NumberAnimation {
                            duration: 150
                        }

                    }

                }

            }

        }

    }

}
