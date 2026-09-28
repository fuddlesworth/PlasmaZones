// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

// The native preview remains live below this readable, unscaled titlebar.
Item {
    id: root
    property var windowInfo: ({})
    property string appName: ""
    property color hue: Appearance.accent
    property bool selected: false
    readonly property string caption: {
        const text = String(windowInfo.title || "");
        for (const separator of [" — ", " – ", " - "]) {
            const suffix = separator + appName;
            if (text.toLowerCase().endsWith(suffix.toLowerCase()))
                return text.slice(0, -suffix.length);
        }
        return text;
    }
    Rectangle {
        anchors.fill: parent
        radius: Appearance.radius
        color: "transparent"
        border.width: 3
        border.color: Appearance.surface
    }
    Item {
        width: parent.width
        height: Math.min(43, parent.height)
        clip: true
        Rectangle {
            width: parent.width
            height: parent.height + radius
            radius: Appearance.radius
            color: Qt.tint(Appearance.surface, Qt.alpha(root.hue, 0.04))
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Appearance.outline
        }
        ShellIcon {
            x: 17
            y: (parent.height - height) / 2
            width: 15
            height: 15
            color: root.hue
            source: root.appName.toLowerCase().includes("firefox") ? "internet-web-browser" : root.appName === "Dolphin" ? "folder" : "utilities-terminal"
        }
        Text {
            id: application
            x: 42
            height: parent.height
            width: Math.min(implicitWidth, Math.max(0, parent.width - x - 95))
            text: root.appName
            color: root.selected ? Appearance.text : Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Text {
            id: separator
            x: application.x + application.width + 10
            height: parent.height
            text: "/"
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            x: separator.x + separator.width + 10
            height: parent.height
            width: Math.max(0, parent.width - x - 95)
            text: root.caption
            color: root.selected ? Appearance.text : Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 24
            height: parent.height
            text: "−     ◇     ×"
            color: Appearance.muted
            font.pixelSize: 9
            verticalAlignment: Text.AlignVCenter
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: Appearance.radius
        color: "transparent"
        border.width: 1
        border.color: Qt.alpha(root.hue, root.selected ? 0.7 : 0.4)
    }
    Rectangle {
        x: Appearance.radius
        width: Math.max(0, parent.width - 2 * x)
        height: root.selected ? 2 : 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: Qt.alpha(root.hue, root.selected ? 0.9 : 0.45)
            }
            GradientStop {
                position: 0.5
                color: Qt.alpha(root.selected ? Appearance.text : root.hue, root.selected ? 0.9 : 0.45)
            }
            GradientStop {
                position: 1
                color: Qt.alpha(root.hue, root.selected ? 0.9 : 0.45)
            }
        }
    }
}
