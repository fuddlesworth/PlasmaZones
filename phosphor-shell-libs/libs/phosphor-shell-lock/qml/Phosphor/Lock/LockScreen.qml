// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var controller: null
    property Component decoration: null
    property var battery: null
    property var keyboard: null
    property var session: null
    property var player: null
    property var spectrum: AudioSpectrum
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
    function revealFocusedControl() {
        if (!Window.window)
            return;
        const item = Window.window.activeFocusItem;
        let ancestor = item;
        while (ancestor && ancestor !== viewport.contentItem)
            ancestor = ancestor.parent;
        if (!ancestor)
            return;
        const point = item.mapToItem(viewport.contentItem, 0, 0);
        if (point.y < viewport.contentY)
            viewport.contentY = Math.max(0, point.y - 8);
        else if (point.y + item.height > viewport.contentY + viewport.height)
            viewport.contentY = Math.min(viewport.contentHeight - viewport.height, point.y + item.height - viewport.height + 8);
    }
    Connections {
        target: root.Window.window
        function onActiveFocusItemChanged() {
            root.revealFocusedControl();
        }
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
            PhosphorMark {
                width: 42
                height: 42
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "PHOSPHOR"
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((10) * Appearance.textScale)
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
                    font.pixelSize: Math.round((11) * Appearance.textScale)
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
                    font.pixelSize: Math.round((11) * Appearance.textScale)
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
            contentHeight: Math.max(height, Math.max(card.y + card.height, extras.y + extras.height) + 4)
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {
                policy: viewport.contentHeight > viewport.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
            LockClock {
                id: clock
                objectName: "lockClock"
                now: root.now
                centered: root.centered
                compact: root.centered && root.height < 720
                timeSize: compact ? 56 : root.centered ? (root.height < 800 ? 80 : 116) : 154
                width: root.centered ? Math.min(600, root.width - 40) : Math.min(600, root.width * .42)
                x: root.centered ? (root.width - width) / 2 : root.width * .091667
                y: root.centered ? Math.max(0, (root.height - 900) / 2 + (root.showMedia ? 82 : 134) - viewport.y) : Math.max(0, root.height * .325556 - viewport.y)
            }
            LockCard {
                id: card
                objectName: "lockCard"
                controller: root.controller
                decoration: root.decoration
                keyboard: root.keyboard
                userName: root.userName
                width: Math.min(382, root.width - root.edgeInset * 2)
                height: implicitHeight
                x: root.centered ? (root.width - width) / 2 : Math.min(root.width - width - root.edgeInset, root.width * .618056)
                y: root.centered ? clock.y + clock.height + (clock.compact ? 18 : root.showMedia ? 46 : 52) : Math.max(0, Math.min(root.height * .331111 - viewport.y, viewport.height - height - 8))
            }
            Column {
                id: extras
                objectName: "lockExtras"
                width: Math.min(402, root.width - root.edgeInset * 2)
                x: root.centered ? (root.width - width) / 2 : root.width * .097222
                y: root.centered ? Math.max(card.y + card.height + 16, root.height - (root.showMedia ? 94 : 103) - viewport.y - height) : Math.max(clock.y + clock.height + 40, root.height - 138 - viewport.y - height)
                spacing: root.centered ? 12 : 19
                Loader {
                    active: root.showMedia
                    visible: active
                    width: parent.width
                    sourceComponent: LockMedia {
                        objectName: "lockMedia"
                        player: root.player
                        spectrum: root.spectrum
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
                            font.pixelSize: Math.round((11) * Appearance.textScale)
                        }
                        Text {
                            text: qsTr("Content hidden while locked")
                            color: Appearance.muted
                            font.family: Tokens.font_family_ui
                            font.pixelSize: Math.round((9) * Appearance.textScale)
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
                source: viewport.contentHeight > viewport.height ? "go-down" : "object-locked"
                color: Appearance.muted
            }
            Text {
                text: viewport.contentHeight > viewport.height ? qsTr("Scroll for more") : qsTr("Your session stays here.")
                color: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((10) * Appearance.textScale)
            }
        }
        MouseArea {
            anchors.fill: parent
            visible: power.open
            onClicked: power.close()
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
