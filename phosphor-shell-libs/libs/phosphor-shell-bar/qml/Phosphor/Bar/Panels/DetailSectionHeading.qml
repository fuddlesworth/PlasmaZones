// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

RowLayout {
    id: root
    property string title: ""
    property string actionText: ""
    property string iconName: "view-refresh"
    property bool actionEnabled: true
    signal activated
    width: parent ? parent.width : 0
    spacing: 8
    DetailText {
        Layout.fillWidth: true
        text: root.title
        size: 12
        font.weight: Font.Medium
    }
    ShellButton {
        visible: text !== ""
        enabled: root.actionEnabled
        text: root.actionText
        iconName: root.iconName
        foreground: Appearance.muted
        labelSize: 11
        flat: true
        onClicked: root.activated()
    }
}
