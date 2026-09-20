// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

LookImage {
    id: root
    property var wallpaper: ({})
    property bool miniature: false
    property bool caption: true
    signal previewRequested
    path: wallpaper.path || ""
    implicitHeight: miniature ? 125 : 240
    Rectangle {
        visible: !root.miniature && root.caption && root.wallpaper.collection === "Added"
        width: root.width * .6
        height: root.height
        radius: root.radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {
                position: 0
                color: "#bf0a0e16"
            }
            GradientStop {
                position: 1
                color: "transparent"
            }
        }
    }
    Rectangle {
        x: root.miniature ? 9 : 14
        y: root.miniature ? 8 : 13
        width: parent.width - x * 2
        height: root.miniature ? 17 : 24
        radius: 6
        color: Qt.alpha(Appearance.surface, 0.82)
        border.color: Appearance.outline
        Text {
            x: 9
            anchors.verticalCenter: parent.verticalCenter
            text: "φ"
            color: Appearance.stops[0]
            font.pixelSize: root.miniature ? 12 : 17
        }
        Rectangle {
            x: 31
            width: parent.width * 0.29
            height: 2
            anchors.verticalCenter: parent.verticalCenter
            color: Qt.alpha(Appearance.muted, 0.25)
        }
        Rectangle {
            x: parent.width * 0.66
            y: 6
            width: 10
            height: 12
            color: Qt.alpha(Appearance.stops[0], 0.2)
            border.color: Appearance.stops[0]
            visible: !root.miniature
        }
        Rectangle {
            x: parent.width * 0.66 + 12
            y: 6
            width: 9
            height: 5
            color: "transparent"
            border.color: Appearance.stops[2]
            visible: !root.miniature
        }
        Rectangle {
            x: parent.width * 0.66 + 12
            y: 13
            width: 9
            height: 5
            color: "transparent"
            border.color: Appearance.stops[3]
            visible: !root.miniature
        }
        LookText {
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: Qt.formatTime(new Date(), "hh:mm")
            size: root.miniature ? 5 : 7
            font.family: Tokens.font_family_mono
        }
    }
    Rectangle {
        x: root.width * 0.49
        y: root.miniature ? 42 : 70
        width: root.width * 0.28
        height: root.height * 0.57
        rotation: -3
        radius: 5
        color: Qt.alpha(Appearance.card, 0.84)
        border.color: Qt.alpha(Appearance.stops[0], 0.4)
        Column {
            x: 9
            y: 12
            width: parent.width - 18
            spacing: root.miniature ? 7 : 10
            Rectangle {
                width: parent.width * 0.22
                height: 3
                color: Qt.alpha(Appearance.stops[0], 0.75)
            }
            Item {
                height: 3
                width: 1
            }
            Repeater {
                model: 3
                Rectangle {
                    required property int index
                    width: parent.width * (index === 1 ? 0.55 : 0.83)
                    height: 3
                    color: Qt.alpha(index === 1 ? Appearance.stops[0] : Appearance.muted, 0.28)
                }
            }
        }
    }
    Column {
        visible: !root.miniature && root.caption
        x: 24
        width: root.width * .43 - 24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 44
        spacing: 10
        LookText {
            text: qsTr("DESKTOP PREVIEW")
            kicker: true
            color: "#c5d8e3"
            font.pixelSize: 7
        }
        LookText {
            width: parent.width
            text: root.wallpaper.name || qsTr("Your desktop")
            wrapMode: Text.NoWrap
            elide: Text.ElideRight
            size: 25
            color: "#e8eef9"
        }
        LookText {
            width: parent.width
            text: root.wallpaper.description || qsTr("Your own view.")
            size: 10
            color: "#cfdae8"
        }
    }
    ShellButton {
        visible: !root.miniature && root.caption
        x: 24
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16
        text: qsTr("View on desktop")
        iconName: "view-grid"
        labelSize: 9
        implicitHeight: 22
        onClicked: root.previewRequested()
    }
}
