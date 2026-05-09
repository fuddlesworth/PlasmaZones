// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

Item {
    // ─── Global state (survives hot-reload, accessible via PhosphorShell.singleton) ─
    PersistentProperties {
        id: shellState

        property bool menuOpen: false
        property bool settingsOpen: false
        property int activeWorkspace: 0

        reloadId: "main"
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
        panelLength: 0 // auto-fit to content
        // implicitWidth feeds auto-fit; bind to content's implicitWidth so the
        // panel grows/shrinks as content changes. Avoids the panel.width ↔
        // childrenRect anchor cycle.
        implicitWidth: leftRow.implicitWidth

        margins {
            left: 8
            top: 6
        }

        ShaderBackground {
            anchors.fill: parent
            playing: true
            shaderSource: Qt.resolvedUrl("shaders/frosted_glass.frag")
            // noiseAmount bumped from 0.025 to 0.18 so the crystalline frost
            // grain is actually visible. animSpeed crawls visibly, noiseScale
            // lowered so cells are bigger / more legible at panel sizes.
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
                    text: shellState.menuOpen ? "✕" : "☰"
                    color: shellState.menuOpen ? "#f38ba8" : "#cdd6f4"
                    font.pixelSize: 14
                }

                MouseArea {
                    id: menuArea

                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: shellState.menuOpen = !shellState.menuOpen
                }

            }

            Row {
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter

                Repeater {
                    model: 5

                    Rectangle {
                        required property int index

                        width: index === shellState.activeWorkspace ? 20 : 8
                        height: 8
                        radius: 4
                        color: index === shellState.activeWorkspace ? "#89b4fa" : "#45475a"

                        MouseArea {
                            anchors.fill: parent
                            onClicked: shellState.activeWorkspace = parent.index
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

    // ─── Center panel segment ────────────────────────────────────────────
    PanelWindow {
        id: centerPanel

        edge: PanelWindow.Top
        thickness: 38
        alignment: PanelWindow.Center
        panelLength: 0 // auto-fit to content
        implicitWidth: clockText.implicitWidth

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
            // Visibly distinct gradient endpoints — Catppuccin mocha mauve →
            // Catppuccin macchiato sky. The previous #1e1e2e → #2e1e2e pair
            // differed only ~16 units in the red channel, producing a flat
            // panel with no visible gradient or animation.
            customColor1: "#cba6f7"
            customColor2: "#89dceb"
        }

        Text {
            id: clockText

            anchors.centerIn: parent
            text: clock.stdout.trim() || "..."
            color: "#1e1e2e"
            font.pixelSize: 13
            font.weight: Font.Bold
            leftPadding: 20
            rightPadding: 20
        }

    }

    // ─── Right panel segment ─────────────────────────────────────────────
    PanelWindow {
        id: rightPanel

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
            playing: true
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
                    text: (cpuUsage.stdout.trim() || "0") + "%"
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
                    text: (memUsage.stdout.trim() || "0") + "%"
                    color: "#cdd6f4"
                    font.pixelSize: 11
                }

            }

            Row {
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter
                visible: battery.exists

                Text {
                    text: "BAT"
                    color: "#f9e2af"
                    font.pixelSize: 11
                    font.weight: Font.Medium
                }

                Text {
                    text: battery.content.trim() + "%"
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
                    onClicked: shellState.settingsOpen = !shellState.settingsOpen
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
        popupVisible: shellState.menuOpen

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
                    model: [{
                        "name": "Terminal",
                        "icon": ">"
                    }, {
                        "name": "Files",
                        "icon": "📁"
                    }, {
                        "name": "Browser",
                        "icon": "🌐"
                    }, {
                        "name": "Editor",
                        "icon": "✎"
                    }, {
                        "name": "Settings",
                        "icon": "⚙"
                    }]

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
                            onClicked: shellState.menuOpen = false
                        }

                    }

                }

            }

        }

    }

    // ─── Bottom taskbar — wlr-foreign-toplevel-management demo ──────────
    PanelWindow {
        id: taskbar

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

    // ─── Floating settings window ────────────────────────────────────────
    FloatingWindow {
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
                    spacing: 8

                    Text {
                        text: "Host: " + hostname.content.trim()
                        color: "#a6adc8"
                        font.pixelSize: 13
                    }

                    Text {
                        text: "User: " + Environment.get("USER")
                        color: "#a6adc8"
                        font.pixelSize: 13
                    }

                    Text {
                        text: "Shell: " + Environment.get("SHELL")
                        color: "#a6adc8"
                        font.pixelSize: 13
                    }

                    Text {
                        text: "Desktop: " + Environment.get("XDG_CURRENT_DESKTOP")
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
                        onClicked: shellState.settingsOpen = false
                    }

                }

            }

        }

    }

}
