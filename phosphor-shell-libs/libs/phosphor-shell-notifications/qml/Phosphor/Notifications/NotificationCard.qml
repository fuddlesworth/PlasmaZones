// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Window
import QtQuick.Effects
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var notification: ({})
    property var backend: null
    property bool arrival: false
    property bool showApp: false
    property bool previews: Appearance.notificationPreviews
    property bool expanded: false
    property bool replying: false
    readonly property color appColor: Appearance.stops[notification.colorIndex === undefined ? 2 : notification.colorIndex]
    readonly property bool unread: !!notification.unread
    readonly property bool held: hover.hovered || activeFocus || expanded || replying
    readonly property var actions: {
        const source = notification.actions;
        if (!source)
            return [];
        if (source.count === undefined)
            return source;
        const result = [];
        for (let i = 0; i < source.count; ++i)
            result.push(source.get(i));
        return result;
    }
    property double now: Date.now()
    readonly property string receivedTime: {
        if (!notification.timestamp)
            return qsTr("Now");
        const minutes = Math.max(0, Math.floor((now - new Date(notification.timestamp).getTime()) / 60000));
        return minutes < 1 ? qsTr("Now") : minutes < 60 ? qsTr("%1 min").arg(minutes) : Qt.formatTime(new Date(notification.timestamp), Qt.locale().timeFormat(Locale.ShortFormat));
    }
    Timer {
        interval: 60000
        repeat: true
        running: root.visible
        onTriggered: root.now = Date.now()
    }
    signal dismissRequested
    signal pauseRequested(bool paused)
    implicitHeight: content.implicitHeight + (arrival ? 35 : 21)
    implicitWidth: arrival ? 396 : 396
    onHeldChanged: pauseRequested(held)
    onPreviewsChanged: if (!previews) {
        replying = false;
        replyInput.clear();
    }
    Component.onDestruction: if (held)
        pauseRequested(false)
    Accessible.role: Accessible.Notification
    Accessible.name: previews ? [notification.appName || "", notification.summary || "", notification.body || ""].join(", ") : qsTr("%1 notification. Preview hidden.").arg(notification.appName || "")
    HoverHandler {
        id: hover
    }
    Rectangle {
        anchors.fill: parent
        visible: !root.arrival
        radius: Appearance.radius * 0.65
        border.color: root.notification.urgency === 2 ? Qt.tint(Appearance.outline, Qt.alpha(root.appColor, 0.3)) : Appearance.outline
        color: Qt.alpha(Appearance.card, root.unread ? 0.45 : 0.32)
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: Qt.alpha(root.appColor, root.notification.urgency === 2 ? 0.12 : root.unread ? 0.07 : 0)
                }
                GradientStop {
                    position: 1
                    color: "transparent"
                }
            }
        }
        Rectangle {
            visible: root.unread
            width: 2
            radius: 1
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 18
            anchors.bottomMargin: 18
            color: root.appColor
        }
    }
    ColumnLayout {
        id: content
        x: root.arrival ? 20 : 14
        y: root.arrival ? 19 : 9
        width: root.width - x * 2
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: root.arrival ? 34 : 25
            spacing: root.arrival ? 11 : 7
            Rectangle {
                visible: root.arrival
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 11
                color: Qt.alpha(root.appColor, 0.18)
                border.color: Qt.alpha(root.appColor, 0.35)
                ShellIcon {
                    anchors.centerIn: parent
                    width: 20
                    height: 20
                    source: root.notification.appIcon || "notifications"
                    isMask: !root.notification.appIcon
                    color: root.appColor
                }
            }
            ColumnLayout {
                visible: root.arrival
                Layout.fillWidth: true
                spacing: 4
                NotificationLabel {
                    Layout.fillWidth: true
                    text: root.notification.appName || qsTr("Notification")
                    color: Appearance.text
                    font.pixelSize: Math.round((11) * Appearance.textScale)
                    font.weight: Font.Medium
                }
                NotificationLabel {
                    text: qsTr("New notification")
                    font.pixelSize: Math.round((8) * Appearance.textScale)
                }
            }
            Rectangle {
                visible: !root.arrival && root.unread
                width: 4
                height: 4
                radius: 2
                color: root.appColor
            }
            ShellIcon {
                visible: !root.arrival && (root.showApp || root.notification.urgency === 2)
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
                source: root.notification.appIcon || "dialog-warning"
                isMask: !root.notification.appIcon
                color: root.appColor
            }
            NotificationLabel {
                visible: !root.arrival
                Layout.fillWidth: true
                text: root.showApp || root.notification.urgency === 2 ? root.notification.appName : root.unread ? qsTr("Unread") : root.notification.live ? qsTr("Received") : qsTr("Saved history")
                font.pixelSize: Math.round((8) * Appearance.textScale)
            }
            NotificationLabel {
                text: root.receivedTime
                font.pixelSize: Math.round((8) * Appearance.textScale)
            }
            ShellButton {
                objectName: "dismissNotification"
                implicitWidth: 23
                implicitHeight: 23
                iconName: "window-close"
                label: qsTr("Dismiss notification")
                flat: true
                foreground: Appearance.muted
                Layout.alignment: Qt.AlignTop
                onClicked: root.dismissRequested()
            }
        }
        NotificationLabel {
            Layout.fillWidth: true
            Layout.topMargin: root.arrival ? 16 : 6
            Layout.bottomMargin: 6
            visible: root.previews && text.length > 0
            text: root.previews ? root.notification.summary || "" : ""
            color: Appearance.text
            font.pixelSize: Math.round((root.arrival ? 14 : 12) * Appearance.textScale)
            font.weight: Font.Medium
            wrapMode: Text.Wrap
            maximumLineCount: root.expanded ? 10000 : 2
        }
        NotificationLabel {
            id: message
            objectName: "notificationMessage"
            Layout.fillWidth: true
            visible: root.previews && text.length > 0
            text: root.previews ? root.notification.body || "" : ""
            font.pixelSize: Math.round((root.arrival ? 11 : 10) * Appearance.textScale)
            lineHeightMode: Text.FixedHeight
            lineHeight: font.pixelSize * 1.7
            wrapMode: Text.Wrap
            maximumLineCount: root.expanded ? 100000 : 3
        }
        NotificationLabel {
            visible: !root.previews
            Layout.topMargin: 8
            text: qsTr("Notification preview hidden")
            Layout.fillWidth: true
        }
        ShellButton {
            objectName: "expandNotification"
            visible: root.previews && (message.truncated || root.expanded)
            Layout.topMargin: 5
            Layout.bottomMargin: 2
            implicitHeight: 24
            text: root.expanded ? qsTr("Show less") : qsTr("Read full message")
            iconName: root.expanded ? "arrow-up" : "arrow-down"
            labelSize: 9
            foreground: root.appColor
            flat: true
            onClicked: root.expanded = !root.expanded
        }
        Rectangle {
            visible: root.previews && String(root.notification.imageSource || "").length > 0
            Layout.fillWidth: true
            Layout.topMargin: 13
            Layout.preferredHeight: Math.min(240, Math.max(1, attachment.sourceSize.height) / Math.max(1, attachment.sourceSize.width) * width)
            radius: Appearance.radius * 0.5
            color: Appearance.recess
            border.color: Appearance.outline
            clip: true
            Rectangle {
                id: imageMask
                anchors.fill: parent
                radius: parent.radius
                color: "white"
                visible: false
                layer.enabled: true
            }
            Image {
                id: attachment
                objectName: "notificationAttachment"
                anchors.fill: parent
                anchors.margins: 1
                source: root.previews ? root.notification.imageSource || "" : ""
                fillMode: Image.PreserveAspectFit
                asynchronous: false
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: imageMask
                }
                Accessible.role: Accessible.Graphic
                Accessible.name: root.previews ? qsTr("Notification image") : ""
                Accessible.ignored: !root.previews
            }
        }
        RowLayout {
            visible: root.previews && (root.actions.length > 0 || root.unread)
            Layout.fillWidth: true
            Layout.topMargin: root.arrival ? 16 : 10
            spacing: 5
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                spacing: 7
                Repeater {
                    model: root.actions
                    ShellButton {
                        required property var modelData
                        objectName: modelData.key === "inline-reply" ? "notificationReplyAction" : "notificationAction"
                        background: Rectangle {
                            radius: 7
                            color: parent.down || parent.hovered ? Qt.alpha(root.appColor, 0.24) : parent.modelData.key === "inline-reply" ? "transparent" : Qt.alpha(root.appColor, 0.12)
                            border.width: 1
                            border.color: parent.visualFocus ? root.appColor : parent.modelData.key === "inline-reply" ? Appearance.outline : "transparent"
                        }
                        text: modelData.label || (modelData.key === "default" ? qsTr("Open") : modelData.key === "inline-reply" ? qsTr("Reply") : modelData.key)
                        implicitHeight: root.arrival ? 29 : 23
                        labelSize: root.arrival ? 10 : 9
                        cornerRadius: 7
                        foreground: modelData.key === "inline-reply" ? Appearance.text : root.appColor
                        outlined: true
                        flat: true
                        onClicked: {
                            if (modelData.key === "inline-reply") {
                                root.replying = !root.replying;
                                if (root.replying)
                                    replyInput.forceActiveFocus();
                            } else if (root.backend)
                                root.backend.activate(root.notification.id, modelData.key, root.Window.window);
                        }
                    }
                }
            }
            ShellButton {
                visible: root.unread
                implicitWidth: 26
                implicitHeight: 26
                flat: true
                iconName: "checkmark"
                label: qsTr("Mark as read")
                foreground: Appearance.muted
                onClicked: if (root.backend)
                    root.backend.markRead(root.notification.id)
            }
        }
        RowLayout {
            visible: root.replying && root.previews && !!root.notification.live
            Layout.fillWidth: true
            Layout.topMargin: 10
            spacing: 7
            TextField {
                id: replyInput
                objectName: "notificationReply"
                Layout.fillWidth: true
                implicitHeight: 34
                placeholderText: root.notification.replyPlaceholder || qsTr("Write a reply…")
                Accessible.name: qsTr("Reply to %1").arg(root.notification.appName || "")
                color: Appearance.text
                placeholderTextColor: Appearance.muted
                font.family: Tokens.font_family_ui
                font.pixelSize: Math.round((11) * Appearance.textScale)
                background: Rectangle {
                    radius: 7
                    color: Appearance.recess
                    border.color: replyInput.activeFocus ? root.appColor : Appearance.outline
                }
                onAccepted: submit.clicked()
                Keys.onEscapePressed: {
                    root.replying = false;
                    root.forceActiveFocus();
                }
            }
            ShellButton {
                id: submit
                objectName: "sendNotificationReply"
                text: root.notification.replyLabel || qsTr("Send")
                enabled: replyInput.text.trim().length > 0
                onClicked: if (enabled && root.backend && root.backend.reply(root.notification.id, replyInput.text)) {
                    replyInput.clear();
                    root.replying = false;
                }
            }
        }
    }
}
