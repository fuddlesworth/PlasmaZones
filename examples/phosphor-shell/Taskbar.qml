// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

// wlr-foreign-toplevel-management demo. Hidden when the compositor
// doesn't advertise the protocol.
PanelWindow {
    id: root

    edge: PanelWindow.Bottom
    thickness: 44
    alignment: PanelWindow.Center
    panelLength: 0
    implicitWidth: Toplevels.supported ? Math.max(taskbarRow.implicitWidth, 200) : 200
    visible: Toplevels.supported

    margins {
        bottom: 6
    }

    ShaderBackground {
        anchors.fill: parent
        playing: true
        shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
        shaderParams: {
            "tintOpacity": 0.8,
            "noiseAmount": 0.15,
            "noiseScale": 20,
            "animSpeed": 0.5,
            "cornerRadius": 12
        }
        customColor1: "#1e1e2e"
    }

    Row {
        id: taskbarRow

        anchors.centerIn: parent
        height: parent.height
        spacing: 6
        leftPadding: 12
        rightPadding: 12

        Repeater {
            model: Toplevels.toplevels

            delegate: Rectangle {
                required property var modelData // PhosphorWayland::ForeignToplevel*

                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(180, Math.max(60, taskLabel.implicitWidth + 24))
                height: 32
                radius: 6
                color: modelData.activated ? "#89b4fa" : (taskMouse.containsMouse ? "#45475a" : "#313244")
                opacity: modelData.minimized ? 0.5 : 1

                Text {
                    id: taskLabel

                    anchors.centerIn: parent
                    anchors.margins: 8
                    width: parent.width - 16
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    text: modelData.title || modelData.appId || "(unnamed)"
                    color: modelData.activated ? "#1e1e2e" : "#cdd6f4"
                    font.pixelSize: 11
                    font.weight: modelData.activated ? Font.Medium : Font.Normal
                }

                MouseArea {
                    id: taskMouse

                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: function(mouse) {
                        if (mouse.button === Qt.MiddleButton) {
                            modelData.close();
                        } else if (modelData.activated) {
                            modelData.setMinimized(true);
                        } else {
                            modelData.setMinimized(false);
                            modelData.activate();
                        }
                    }
                }

                Behavior on color {
                    ColorAnimation {
                        duration: 120
                    }

                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: 120
                    }

                }

            }

        }

    }

}
