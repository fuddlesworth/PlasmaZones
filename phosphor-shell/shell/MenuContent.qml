// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

// Menu content — body of the application-launcher panel popup.
// Wrapped by PanelPopupHost.
Item {
    id: root

    required property var shellState
    property bool active: false

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

    scale: active ? 1 : 0.85
    opacity: active ? 1 : 0

    Column {
        anchors.fill: parent
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
                        color: "#1e1e2e"
                        font.pixelSize: 13
                    }
                }

                MouseArea {
                    id: itemMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    Accessible.role: Accessible.MenuItem
                    Accessible.name: modelData.name
                    onClicked: root.shellState.togglePopup("menu")
                }
            }
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: 300
            easing.type: Easing.OutBack
            easing.overshoot: 1.2
        }
    }
    Behavior on opacity {
        NumberAnimation {
            duration: 200
        }
    }
}
