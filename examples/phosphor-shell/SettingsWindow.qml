// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

FloatingWindow {
    id: root

    required property var shellState
    required property string hostname

    title: "PhosphorShell Settings"
    windowWidth: 400
    windowHeight: 300
    windowVisible: shellState.settingsOpen

    Rectangle {
        anchors.fill: parent
        color: "#1e1e2e"

        Column {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 16

            Text {
                text: "PhosphorShell Settings"
                color: "#cdd6f4"
                font.pixelSize: 18
                font.weight: Font.Bold
            }

            Rectangle {
                width: parent.width
                height: 1
                color: "#313244"
            }

            Column {
                id: envCol

                // Environment.get returns "" for unset vars; the helper
                // below prints "(unset)" so missing env vars don't render
                // as a literal "User: undefined" / "User: " string.
                function envOr(name) {
                    var v = Environment.get(name);
                    return (v === undefined || v === "") ? "(unset)" : v;
                }

                spacing: 8

                Text {
                    text: "Host: " + (root.hostname || "(unknown)")
                    color: "#a6adc8"
                    font.pixelSize: 13
                }

                Text {
                    text: "User: " + envCol.envOr("USER")
                    color: "#a6adc8"
                    font.pixelSize: 13
                }

                Text {
                    text: "Shell: " + envCol.envOr("SHELL")
                    color: "#a6adc8"
                    font.pixelSize: 13
                }

                Text {
                    text: "Desktop: " + envCol.envOr("XDG_CURRENT_DESKTOP")
                    color: "#a6adc8"
                    font.pixelSize: 13
                }

            }

            Rectangle {
                width: parent.width
                height: 1
                color: "#313244"
            }

            Text {
                text: "Edit ~/.config/phosphor-shell/shell.qml to customize.\nChanges apply instantly via hot-reload."
                color: "#6c7086"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                width: parent.width
            }

            Rectangle {
                width: 80
                height: 30
                radius: 6
                color: closeArea.containsMouse ? "#45475a" : "#313244"

                Text {
                    anchors.centerIn: parent
                    text: "Close"
                    color: "#cdd6f4"
                    font.pixelSize: 12
                }

                MouseArea {
                    id: closeArea

                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    Accessible.role: Accessible.Button
                    Accessible.name: "Close settings window"
                    onClicked: root.shellState.settingsOpen = false
                }

            }

        }

    }

}
