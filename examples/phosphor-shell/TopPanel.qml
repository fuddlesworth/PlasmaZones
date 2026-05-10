// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Single continuous top panel: one layer-shell surface spans the full
// screen edge, three child zones anchor to left / center / right.

import Phosphor.Shell 1.0
import QtQuick

// Trade-off vs the three-panel layout: one exclusive zone, one shader,
// one wl_surface — cheaper for the compositor. Anchors don't reserve
// space, so a very long center zone CAN overlap left/right; pick this
// pattern when "absolutely centered clock" matters more than overlap
// avoidance.
PanelWindow {
    id: root

    // Anchors exposed for popup positioning.
    property alias menuAnchor: menuButton
    property alias calendarAnchor: clockLabel
    required property var shellState
    required property string clockText
    required property string cpuPercent
    required property string memPercent
    required property string batteryPercent
    required property bool batteryVisible

    edge: PanelWindow.Top
    thickness: 38
    alignment: PanelWindow.Fill
    exclusiveZoneEnabled: true

    ShaderBackground {
        anchors.fill: parent
        playing: root.visible
        shaderSource: Qt.resolvedUrl("shaders/gradient.frag")
        shaderParams: {
            "speed": 1.2,
            "angle": 0,
            "cornerRadius": 0
        }
        // Catppuccin mocha mauve → macchiato sky.
        customColor1: "#cba6f7"
        customColor2: "#89dceb"
    }

    // ─── Left zone: menu button + workspace dots ─────────────────────
    Row {
        id: leftZone

        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 12
        spacing: 12

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
                color: root.shellState.menuOpen ? "#f38ba8" : "#1e1e2e"
                font.pixelSize: 14
                font.weight: Font.Bold
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

            // NOTE: this example uses indices for "workspaces" which is
            // pedagogical only — production shells should bind to
            // compositor-provided workspace IDs (per the project's
            // "Zone IDs, never indices" rule).
            Repeater {
                model: 5

                Rectangle {
                    required property int index

                    width: index === root.shellState.activeWorkspace ? 20 : 8
                    height: 8
                    radius: 4
                    color: index === root.shellState.activeWorkspace ? "#1e1e2e" : "#6c7086"

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

    // ─── Center zone: clock + calendar trigger ───────────────────────
    Text {
        id: clockLabel

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        // U+2026 ELLIPSIS while the Process is producing its first output.
        text: root.clockText || "…"
        color: "#1e1e2e"
        font.pixelSize: 13
        font.weight: Font.Bold
        leftPadding: 20
        rightPadding: 20

        // MouseArea hosted on Text — clickable region tracks the Text's
        // implicit-size + padding box. Hit testing is geometry-driven,
        // so a Text item is a valid (if unconventional) MouseArea host;
        // anchoring the trigger to clockLabel matches `calendarAnchor`
        // so the popup originates visually from the clock.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            Accessible.role: Accessible.Button
            Accessible.name: root.shellState.calendarOpen ? "Close calendar" : "Open calendar"
            onClicked: root.shellState.calendarOpen = !root.shellState.calendarOpen
        }

    }

    // ─── Right zone: CPU / MEM / BAT readouts + settings ─────────────
    Row {
        id: rightZone

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.rightMargin: 12
        spacing: 14

        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: "CPU"
                color: "#1e1e2e"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                text: (root.cpuPercent || "0") + "%"
                color: "#1e1e2e"
                font.pixelSize: 11
            }

        }

        Row {
            spacing: 4
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: "MEM"
                color: "#1e1e2e"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                text: (root.memPercent || "0") + "%"
                color: "#1e1e2e"
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
                color: "#1e1e2e"
                font.pixelSize: 11
                font.weight: Font.Medium
            }

            Text {
                text: root.batteryPercent + "%"
                color: "#1e1e2e"
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
                color: "#1e1e2e"
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
