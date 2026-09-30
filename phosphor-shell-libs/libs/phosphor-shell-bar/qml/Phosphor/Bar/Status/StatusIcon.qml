// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    required property var entry
    property color tint: Appearance.text
    implicitWidth: 18
    implicitHeight: 18
    Accessible.ignored: true
    ShellIcon {
        anchors.fill: parent
        source: root.entry.iconName || "configure"
        isMask: true
        color: root.tint
    }
    Rectangle {
        visible: !!root.entry.off
        anchors.centerIn: parent
        width: root.width * 1.3
        height: 2
        radius: height / 2
        rotation: -45
        color: root.tint
    }
    Rectangle {
        visible: !!root.entry.attention
        anchors.right: parent.right
        anchors.top: parent.top
        width: 6
        height: width
        radius: width / 2
        color: Appearance.stops[3]
        border.width: 1
        border.color: Appearance.card
    }
}
