// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

Item {
    // ─── Global state (survives hot-reload, accessible via PhosphorShell.singleton) ─
    PersistentProperties {
        id: state
        reloadId: "main"
        property bool menuOpen: false
        property bool settingsOpen: false
        property int activeWorkspace: 0
    }

    // ─── System data sources ──────────────────────────────────────────────
    Process {
        id: clock
        command: ["date", "+%H:%M · %a %b %d"]
        running: true
        interval: 10000
    }

    Process {
        id: cpuUsage
        command: ["sh", "-c", "grep 'cpu ' /proc/stat | awk '{usage=($2+$4)*100/($2+$4+$5)} END {printf \"%.0f\", usage}'"]
        running: true
        interval: 2000
    }

    Process {
        id: memUsage
        command: ["sh", "-c", "free | awk '/Mem:/ {printf \"%.0f\", $3/$2*100}'"]
        running: true
        interval: 5000
    }

    FileView {
        id: battery
        path: "/sys/class/power_supply/BAT0/capacity"
        interval: 30000
    }

    FileView {
        id: hostname
        path: "/etc/hostname"
    }

    // ─── Left panel segment ──────────────────────────────────────────────
    PanelWindow {
        id: leftPanel
        edge: PanelWindow.Top
        thickness: 38
        alignment: PanelWindow.Start
        panelLength: 240
        margins { left: 8; top: 6 }

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 12
            clip: true
            border.color: "#313244"
            border.width: 1

            ShaderBackground {
                anchors.fill: parent
                shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
                shaderParams: { "tintOpacity": 0.75, "noiseAmount": 0.025, "noiseScale": 45.0, "animSpeed": 0.2 }
                customColor1: "#1e1e2e"
            }

            Row {
                anchors.centerIn: parent
                spacing: 12

                // Menu button
                Rectangle {
                    id: menuButton
                    width: 30; height: 30
                    radius: 8
                    color: menuArea.containsMouse ? "#45475a" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: state.menuOpen ? "✕" : "☰"
                        color: state.menuOpen ? "#f38ba8" : "#cdd6f4"
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: menuArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: state.menuOpen = !state.menuOpen
                    }
                }

                // Workspaces
                Row {
                    spacing: 6
                    Repeater {
                        model: 5
                        Rectangle {
                            required property int index
                            width: index === state.activeWorkspace ? 20 : 8
                            height: 8
                            radius: 4
                            color: index === state.activeWorkspace ? "#89b4fa" : "#45475a"

                            Behavior on width { NumberAnimation { duration: 150 } }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: state.activeWorkspace = parent.index
                            }
                        }
                    }
                }
            }
        }
    }

    // ─── Center panel segment ────────────────────────────────────────────
    PanelWindow {
        id: centerPanel
        edge: PanelWindow.Top
        thickness: 38
        alignment: PanelWindow.Center
        panelLength: 220
        margins { top: 6 }

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 12
            clip: true
            border.color: "#313244"
            border.width: 1

            ShaderBackground {
                anchors.fill: parent
                shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
                shaderParams: { "speed": 0.3 }
                customColor1: "#1e1e2e"
                customColor2: "#2e1e2e"
            }

            Text {
                anchors.centerIn: parent
                text: clock.stdout.trim() || "..."
                color: "#cdd6f4"
                font.pixelSize: 13
                font.weight: Font.Medium
            }
        }
    }

    // ─── Right panel segment ─────────────────────────────────────────────
    PanelWindow {
        id: rightPanel
        edge: PanelWindow.Top
        thickness: 38
        alignment: PanelWindow.End
        panelLength: 280
        margins { right: 8; top: 6 }

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 12
            clip: true
            border.color: "#313244"
            border.width: 1

            ShaderBackground {
                anchors.fill: parent
                shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
                shaderParams: { "tintOpacity": 0.75, "noiseAmount": 0.02, "noiseScale": 50.0, "animSpeed": 0.15 }
                customColor1: "#1e1e2e"
            }

            Row {
                anchors.centerIn: parent
                spacing: 14

                // CPU
                Row {
                    spacing: 4
                    Text { text: "CPU"; color: "#a6e3a1"; font.pixelSize: 11; font.weight: Font.Medium }
                    Text {
                        text: (cpuUsage.stdout.trim() || "0") + "%"
                        color: "#cdd6f4"
                        font.pixelSize: 11
                    }
                }

                // Memory
                Row {
                    spacing: 4
                    Text { text: "MEM"; color: "#89dceb"; font.pixelSize: 11; font.weight: Font.Medium }
                    Text {
                        text: (memUsage.stdout.trim() || "0") + "%"
                        color: "#cdd6f4"
                        font.pixelSize: 11
                    }
                }

                // Battery (only on laptops)
                Row {
                    spacing: 4
                    visible: battery.exists
                    Text { text: "BAT"; color: "#f9e2af"; font.pixelSize: 11; font.weight: Font.Medium }
                    Text {
                        text: battery.content.trim() + "%"
                        color: "#cdd6f4"
                        font.pixelSize: 11
                    }
                }

                // Settings button
                Rectangle {
                    width: 26; height: 26
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
                        onClicked: state.settingsOpen = !state.settingsOpen
                    }
                }
            }
        }
    }

    // ─── App launcher popup ──────────────────────────────────────────────
    PopupWindow {
        id: menuPopup
        anchor: menuButton
        popupEdge: PopupWindow.Below
        popupWidth: 220
        popupHeight: 240
        gap: 8
        popupVisible: state.menuOpen

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 12
            border.color: "#313244"
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                Text {
                    text: "Applications"
                    color: "#6c7086"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                    leftPadding: 8
                    bottomPadding: 4
                }

                Repeater {
                    model: [
                        { name: "Terminal", icon: ">" },
                        { name: "Files", icon: "📁" },
                        { name: "Browser", icon: "🌐" },
                        { name: "Editor", icon: "✎" },
                        { name: "Settings", icon: "⚙" }
                    ]

                    Rectangle {
                        required property var modelData
                        width: parent.width
                        height: 34
                        radius: 8
                        color: itemMouse.containsMouse ? "#313244" : "transparent"

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 10
                            spacing: 10

                            Text {
                                text: modelData.icon
                                color: "#89b4fa"
                                font.pixelSize: 14
                            }
                            Text {
                                text: modelData.name
                                color: "#cdd6f4"
                                font.pixelSize: 13
                            }
                        }

                        MouseArea {
                            id: itemMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: state.menuOpen = false
                        }
                    }
                }
            }
        }
    }

    // ─── Floating settings window ────────────────────────────────────────
    FloatingWindow {
        title: "PhosphorShell Settings"
        windowWidth: 400
        windowHeight: 300
        windowVisible: state.settingsOpen

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

                Rectangle { width: parent.width; height: 1; color: "#313244" }

                Column {
                    spacing: 8
                    Text { text: "Host: " + hostname.content.trim(); color: "#a6adc8"; font.pixelSize: 13 }
                    Text { text: "User: " + Environment.get("USER"); color: "#a6adc8"; font.pixelSize: 13 }
                    Text { text: "Shell: " + Environment.get("SHELL"); color: "#a6adc8"; font.pixelSize: 13 }
                    Text { text: "Desktop: " + Environment.get("XDG_CURRENT_DESKTOP"); color: "#a6adc8"; font.pixelSize: 13 }
                }

                Rectangle { width: parent.width; height: 1; color: "#313244" }

                Text {
                    text: "Edit ~/.config/phosphor-shell/shell.qml to customize.\nChanges apply instantly via hot-reload."
                    color: "#6c7086"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    width: parent.width
                }

                Rectangle {
                    width: 80; height: 30
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
                        onClicked: state.settingsOpen = false
                    }
                }
            }
        }
    }
}
