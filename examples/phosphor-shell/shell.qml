// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PhosphorShell 1.0
import QtQuick

Item {
    // ─── State that survives hot-reload ───────────────────────────────────
    PersistentProperties {
        id: state
        reloadId: "main"
        property bool menuOpen: false
    }

    // ─── System data sources ──────────────────────────────────────────────
    Process {
        id: clock
        command: ["date", "+%H:%M"]
        running: true
        interval: 30000
    }

    Process {
        id: cpuUsage
        command: ["sh", "-c", "grep 'cpu ' /proc/stat | awk '{usage=($2+$4)*100/($2+$4+$5)} END {printf \"%.0f\", usage}'"]
        running: true
        interval: 2000
    }

    FileView {
        id: battery
        path: "/sys/class/power_supply/BAT0/capacity"
        interval: 10000
    }

    // ─── Left panel segment ──────────────────────────────────────────────
    PanelWindow {
        id: leftPanel
        edge: PanelWindow.Top
        thickness: 36
        alignment: PanelWindow.Start
        panelLength: 220
        margins { left: 8; top: 6 }

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 10
            border.color: "#313244"
            border.width: 1

            Row {
                anchors.centerIn: parent
                spacing: 12

                // Menu button
                Rectangle {
                    id: menuButton
                    width: 28; height: 28
                    radius: 6
                    color: menuArea.containsMouse ? "#45475a" : "transparent"

                    Text {
                        anchors.centerIn: parent
                        text: "☰"
                        color: "#cdd6f4"
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: menuArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: state.menuOpen = !state.menuOpen
                    }
                }

                // Workspaces placeholder
                Row {
                    spacing: 4
                    Repeater {
                        model: 4
                        Rectangle {
                            width: 8; height: 8
                            radius: 4
                            color: index === 0 ? "#89b4fa" : "#45475a"
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
        thickness: 36
        alignment: PanelWindow.Center
        panelLength: 160
        margins { top: 6 }

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 10
            border.color: "#313244"
            border.width: 1

            Text {
                anchors.centerIn: parent
                text: clock.stdout.trim() || "..."
                color: "#cdd6f4"
                font.pixelSize: 14
                font.weight: Font.Medium
            }
        }
    }

    // ─── Right panel segment ─────────────────────────────────────────────
    PanelWindow {
        id: rightPanel
        edge: PanelWindow.Top
        thickness: 36
        alignment: PanelWindow.End
        panelLength: 240
        margins { right: 8; top: 6 }

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 10
            border.color: "#313244"
            border.width: 1

            Row {
                anchors.centerIn: parent
                spacing: 16

                // CPU
                Row {
                    spacing: 4
                    Text { text: "⚙"; color: "#a6e3a1"; font.pixelSize: 12 }
                    Text {
                        text: (cpuUsage.stdout.trim() || "0") + "%"
                        color: "#cdd6f4"
                        font.pixelSize: 12
                    }
                }

                // Battery
                Row {
                    spacing: 4
                    visible: battery.exists
                    Text { text: "⚡"; color: "#f9e2af"; font.pixelSize: 12 }
                    Text {
                        text: battery.content.trim() + "%"
                        color: "#cdd6f4"
                        font.pixelSize: 12
                    }
                }

                // User
                Text {
                    text: Environment.get("USER")
                    color: "#89b4fa"
                    font.pixelSize: 12
                }
            }
        }
    }

    // ─── Dropdown menu (popup) ───────────────────────────────────────────
    PopupWindow {
        id: menuPopup
        anchor: menuButton
        popupEdge: PopupWindow.Below
        popupWidth: 200
        popupHeight: 180
        gap: 6
        popupVisible: state.menuOpen

        Rectangle {
            anchors.fill: parent
            color: "#1e1e2e"
            radius: 10
            border.color: "#313244"
            border.width: 1

            Column {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                Repeater {
                    model: ["Terminal", "Files", "Browser", "Settings"]

                    Rectangle {
                        width: parent.width
                        height: 32
                        radius: 6
                        color: itemMouse.containsMouse ? "#313244" : "transparent"

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            text: modelData
                            color: "#cdd6f4"
                            font.pixelSize: 13
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
}
