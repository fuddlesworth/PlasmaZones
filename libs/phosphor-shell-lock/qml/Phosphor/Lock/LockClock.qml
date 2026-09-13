// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Column {
    id: root
    property date now: new Date()
    property bool centered: false
    property int timeSize: centered ? 116 : 154
    spacing: 0
    Text {
        width: parent.width
        leftPadding: root.centered ? 0 : 8
        horizontalAlignment: root.centered ? Text.AlignHCenter : Text.AlignLeft
        text: Qt.formatDate(root.now, "dddd, MMMM d").toUpperCase()
        color: Appearance.muted
        font.family: Tokens.font_family_ui
        font.pixelSize: 12
        font.letterSpacing: 2.5
        elide: Text.ElideRight
    }
    Text {
        width: parent.width
        height: root.timeSize * (root.centered ? 1.25 : 1.3)
        horizontalAlignment: root.centered ? Text.AlignHCenter : Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        text: Qt.formatTime(root.now, "HH") + '<font color="' + Appearance.accent + '">:</font>' + Qt.formatTime(root.now, "mm")
        textFormat: Text.RichText
        color: Appearance.text
        font.family: Tokens.font_family_ui
        font.features: {
            "tnum": 1
        }
        font.pixelSize: root.timeSize
        font.weight: Font.Light
        font.letterSpacing: -root.timeSize * .058
        Accessible.name: Qt.formatTime(root.now, "HH:mm")
    }
    Row {
        x: root.centered ? (parent.width - width) / 2 : 9
        topPadding: root.centered ? 8 : 12
        spacing: 13
        Item {
            width: 30
            height: 21
            Repeater {
                model: [Qt.rect(0, 0, 14, 21), Qt.rect(18, 0, 11, 9), Qt.rect(18, 12, 11, 9)]
                Rectangle {
                    required property rect modelData
                    required property int index
                    x: modelData.x
                    y: modelData.y
                    width: modelData.width
                    height: modelData.height
                    radius: 2
                    border.width: 1
                    border.color: Appearance.windowColor(index)
                    color: index === 0 ? Qt.alpha(Appearance.stops[0], .12) : "transparent"
                }
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Your windows, in place.")
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: 12
            font.letterSpacing: .3
        }
    }
}
