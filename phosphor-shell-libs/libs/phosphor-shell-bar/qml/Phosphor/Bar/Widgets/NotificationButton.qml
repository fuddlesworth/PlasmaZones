// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

BarIconButton {
    iconName: "notifications"
    label: qsTr("Notifications, %1 unread").arg(NotificationRegistry.unreadCount)
    Rectangle {
        visible: NotificationRegistry.unreadCount > 0
        anchors.right: parent.right
        anchors.top: parent.top
        width: 7
        height: 7
        radius: 4
        color: Appearance.stops[2]
        border.width: 1
        border.color: Appearance.surface
    }
}
