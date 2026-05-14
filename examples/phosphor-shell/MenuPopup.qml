// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import Phosphor.Shell 1.0
import QtQuick

PopupWindow {
    id: root

    required property var shellState
    required property var anchorItem
    // Menu items hoisted to a property so the binding is evaluated once,
    // not every time the Repeater re-runs the inline literal — re-running
    // would rebuild every delegate and reset hover state.
    readonly property var menuItems: [
        {
            "name": "Terminal",
            "icon": ">"
        },
        {
            "name": "Files",
            "icon": "📁"
        },
        {
            "name": "Browser",
            "icon": "🌐"
        },
        {
            "name": "Editor",
            "icon": "✎"
        },
        {
            "name": "Settings",
            "icon": "⚙"
        }
    ]

    anchor: anchorItem
    popupEdge: PopupWindow.Below
    popupWidth: 220
    popupHeight: 240
    gap: 8
    // Imperative-driven from shellState (see CalendarPopup.qml for
    // the rationale — direct binding loops on compositor dismissal).
    popupVisible: false
    onPopupVisibleChanged: {
        if (!popupVisible && shellState.menuOpen)
            shellState.menuOpen = false;
    }

    Connections {
        // See CalendarPopup.qml for the deferred-open rationale.
        function onMenuOpenChanged() {
            if (shellState.menuOpen)
                Qt.callLater(() => root.popupVisible = shellState.menuOpen);
            else
                root.popupVisible = false;
        }

        target: shellState
    }

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
                model: root.menuItems

                delegate: Rectangle {
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
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: modelData.name
                        onClicked: root.shellState.menuOpen = false
                    }
                }
            }
        }
    }
}
