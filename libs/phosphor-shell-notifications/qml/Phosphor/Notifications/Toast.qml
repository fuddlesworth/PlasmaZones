// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root
    property var notification: ({})
    property var backend: null
    property Component decoration: null
    property bool previews: Appearance.notificationPreviews
    property int timeout: notification.timeout === undefined ? 5000 : notification.timeout
    readonly property bool held: card.held
    signal dismissed
    signal openCenterRequested
    signal pauseRequested(bool paused)
    implicitWidth: 396
    implicitHeight: card.implicitHeight + 45
    DecorationSlot {
        anchors.fill: parent
        component: root.decoration
        contentItem: surface
        surfacePath: "shell.phosphor.notification"
        focused: root.held
    }
    ShellSurface {
        id: surface
        property bool shaderAnchor: true
        anchors.fill: parent
        accented: true
        border.color: root.notification.urgency === 2 ? Qt.alpha(Appearance.stops[3], 0.45) : Appearance.outline
    }
    NotificationCard {
        id: card
        width: parent.width
        height: implicitHeight
        notification: root.notification
        backend: root.backend
        arrival: true
        previews: root.previews
        onDismissRequested: root.dismissed()
        onPauseRequested: paused => root.pauseRequested(paused)
    }
    Rectangle {
        x: 1
        y: card.height
        width: parent.width - 2
        height: 1
        color: Appearance.outline
    }
    NotificationLabel {
        x: 20
        anchors.verticalCenter: footer.verticalCenter
        text: root.notification.transient ? qsTr("Temporary notification") : qsTr("Saved in your notification center")
        font.pixelSize: 8
        width: Math.max(0, root.width - 150)
    }
    ShellButton {
        id: footer
        x: root.width - width - 16
        y: card.height + 7
        text: qsTr("Open center")
        iconName: "go-next"
        flat: true
        labelSize: 9
        onClicked: root.openCenterRequested()
    }
    DragHandler {
        id: swipe
        target: null
        yAxis.enabled: false
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        onActiveChanged: if (!active && translation.x > 72)
            root.dismissed()
    }
    Timer {
        interval: Math.max(1, root.timeout)
        running: root.timeout > 0 && !root.held
        onTriggered: root.dismissed()
    }
}
