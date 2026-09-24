// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Theme
import Phosphor.Shell

AbstractButton {
    id: root
    signal activated
    property real railT: 0.8
    property bool expanded: false
    SystemClock {
        id: clock
        precision: SystemClock.Minutes
    }
    readonly property string timeText: String(clock.hours).padStart(2, "0") + ":" + String(clock.minutes).padStart(2, "0")
    implicitWidth: labels.implicitWidth + 16
    leftPadding: 8
    rightPadding: 8
    implicitHeight: 32
    Accessible.name: qsTr("Show calendar") + " " + dateLabel.text + " " + timeText
    onClicked: activated()
    background: Rectangle {
        radius: 8
        border.width: root.visualFocus ? 1 : 0
        border.color: Appearance.text
        color: root.expanded ? Appearance.card : root.hovered ? Qt.alpha(Appearance.text, 0.05) : "transparent"
        Rectangle {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 8
            height: 2
            color: Appearance.stops[2]
            visible: root.expanded
        }
    }
    contentItem: Row {
        id: labels
        spacing: 7
        Text {
            id: dateLabel
            text: Qt.formatDate(clock.date, "ddd d")
            color: Appearance.muted
            font.family: Tokens.font_family_ui
            font.pixelSize: Math.round((11) * Appearance.textScale)
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.timeText
            color: Appearance.text
            font.family: Tokens.font_family_mono
            font.pixelSize: Math.round((11) * Appearance.textScale)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
