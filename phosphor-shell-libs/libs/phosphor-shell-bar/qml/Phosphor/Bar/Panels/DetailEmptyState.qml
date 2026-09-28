// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property string title: ""
    property string description: ""
    property string iconName: ""
    property string actionText: ""
    signal activated
    width: parent ? parent.width : 0
    implicitHeight: Math.max(315, layout.implicitHeight + 50)
    ColumnLayout {
        id: layout
        anchors.centerIn: parent
        width: Math.max(0, parent.width - 50)
        spacing: 16
        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 80
            Layout.preferredHeight: 80
            radius: 40
            color: Qt.alpha(Appearance.stops[0], 0.04)
            border.width: 1
            border.color: Appearance.outline
            ShellIcon {
                anchors.centerIn: parent
                width: 32
                height: 32
                source: root.iconName
                isMask: true
                color: Appearance.muted
            }
        }
        DetailText {
            Layout.fillWidth: true
            text: root.title
            horizontalAlignment: Text.AlignHCenter
            size: 20
            font.weight: Font.Medium
        }
        DetailText {
            Layout.fillWidth: true
            text: root.description
            horizontalAlignment: Text.AlignHCenter
            muted: true
            lineHeight: 1.4
        }
        ShellButton {
            visible: text !== ""
            Layout.alignment: Qt.AlignHCenter
            text: root.actionText
            implicitHeight: 34
            onClicked: root.activated()
        }
    }
}
