// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var controller: null
    property var battery: null
    property var keyboard: null
    property var session: null
    property var player: null
    property string userName: ""
    property int notificationCount: 0
    property date now: new Date()
    readonly property bool centered: Appearance.lockLayout === "centered" || width < 1100
    readonly property bool showMedia: Appearance.lockMedia && player !== null
    readonly property bool showNotifications: Appearance.lockNotifications && notificationCount > 0
    readonly property int edgeInset: width < 600 ? 20 : 48
    readonly property string clockText: Qt.formatTime(now, "HH:mm")
    focus: true
    clip: true
    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Locked")
    Timer {
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.now = new Date()
    }
    function focusPassword() {
        card.focusPassword();
    }
    Component.onCompleted: focusPassword()
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape && power.open) {
            power.close();
            event.accepted = true;
        } else if (root.controller && root.controller.handleKey(event)) {
            event.accepted = true;
            root.focusPassword();
        }
    }
    Connections {
        target: root.controller
        function onFailed() {
            root.focusPassword();
        }
    }
    LockBackdrop {
        anchors.fill: parent
    }
    Item {
        id: content
        anchors.fill: parent
        opacity: root.controller && root.controller.dismissing ? 0 : 1
        Behavior on opacity {
            NumberAnimation {
                duration: root.controller ? root.controller.dismissDuration : 0
                easing: Motion.dismiss
            }
        }
        Row {
            x: root.edgeInset
            y: 40
            spacing: 14
            Text {
                text: "φ"
                color: Appearance.stops[0]
                font.family: Tokens.font_family_ui
                font.pixelSize: 31
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "PHOSPHOR"
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 10
                font.letterSpacing: 3.5
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: root.edgeInset
            y: 51
            spacing: 28
            Row {
                spacing: 8
                visible: root.width >= 600
                ShellIcon {
                    width: 16
                    height: 16
                    source: "object-locked"
                    color: Appearance.muted
                }
                Text {
                    text: qsTr("Session locked")
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                }
            }
            Row {
                readonly property var device: root.battery ? root.battery.displayDevice : null
                visible: !!device && device.isPresent && Number.isFinite(device.percentage)
                spacing: 8
                ShellIcon {
                    width: 16
                    height: 16
                    source: "battery"
                    color: Appearance.muted
                }
                Text {
                    text: parent.device ? Math.round(parent.device.percentage) + "%" : ""
                    color: Appearance.muted
                    font.family: Tokens.font_family_ui
                    font.pixelSize: 11
                }
            }
        }
        // The composition keeps the reference positions on a 1440 x 900 output.
        // Short outputs scroll the central content, leaving power always reachable.
        Flickable {
            id: viewport
            x: 0
            y: 80
            width: parent.width
            height: Math.max(1, parent.height - 170)
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, Math.max(card.y + card.height, extras.y + extras.height) + 24)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: viewport.contentHeight > viewport.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
            LockClock {
                id: clock
                objectName: "lockClock"
                now: root.now
                centered: root.centered
                timeSize: root.centered ? (root.height < 800 ? 80 : 116) : 154
                width: root.centered ? Math.min(600, root.width - 40) : Math.min(600, root.width * .42)
                x: root.centered ? (root.width - width) / 2 : root.width * .091667
                y: root.centered ? Math.max(0, (root.height - 900) / 2 + (root.showMedia ? 82 : 134) - viewport.y) : Math.max(0, root.height * .325556 - viewport.y)
            }
            LockCard {
                id: card
                objectName: "lockCard"
                controller: root.controller
                keyboard: root.keyboard
                userName: root.userName
                width: Math.min(382, root.width - root.edgeInset * 2)
                height: implicitHeight
                x: root.centered ? (root.width - width) / 2 : Math.min(root.width - width - root.edgeInset, root.width * .618056)
                y: root.centered ? clock.y + clock.height + (root.showMedia ? 50 : 56) : Math.max(0, root.height * .331111 - viewport.y)
            }
            Column {
                id: extras
                objectName: "lockExtras"
                width: Math.min(402, root.width - root.edgeInset * 2)
                x: root.centered ? (root.width - width) / 2 : root.width * .097222
                y: root.centered ? card.y + card.height + 24 : Math.max(clock.y + clock.height + 40, root.height - 138 - viewport.y - height)
                spacing: root.centered ? 12 : 19
                Loader {
                    active: root.showMedia
                    visible: active
                    width: parent.width
                    sourceComponent: LockMedia {
                        objectName: "lockMedia"
                        player: root.player
                    }
                }
                Row {
                    objectName: "lockNotificationCount"
                    visible: root.showNotifications
                    x: root.centered ? (parent.width - width) / 2 : 0
                    spacing: 13
                    ShellIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 17
                        height: 17
                        source: "notifications"
                        color: Appearance.muted
                    }
                    Column {
                        spacing: 5
                        Text {
                            text: root.notificationCount === 1 ? qsTr("1 notification") : qsTr("%1 notifications").arg(root.notificationCount)
                            color: Appearance.text
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 11
                        }
                        Text {
                            text: qsTr("Content hidden while locked")
                            color: Appearance.muted
                            font.family: Tokens.font_family_ui
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }
        Row {
            x: root.edgeInset
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 48
            spacing: 8
            ShellIcon {
                width: 13
                height: 13
                source: "object-locked"
                color: Appearance.muted
            }
            Text {
                text: qsTr("Your session stays here.")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: 10
            }
        }
        LockPowerMenu {
            id: power
            objectName: "lockPower"
            anchors.right: parent.right
            anchors.rightMargin: root.edgeInset
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 36
            session: root.session
            enabled: !root.controller || (!root.controller.authenticating && !root.controller.dismissing)
            onClosed: root.focusPassword()
        }
    }
}
