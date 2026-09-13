// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root
    property var backend: null
    property bool unreadOnly: false
    property var expandedGroups: []
    property real railT: 0.8
    property bool previews: Appearance.notificationPreviews
    property bool byApp: Appearance.notificationGrouping === "app"
    readonly property int total: backend ? backend.count : 0
    readonly property int unread: backend ? backend.unreadCount : 0
    readonly property bool available: backend ? backend.serverActive : false
    signal closeRequested
    implicitWidth: 444
    implicitHeight: Math.max(180, Screen.height - Appearance.barHeight - 52)
    readonly property int popoutTopInset: Appearance.bottom ? 24 : Appearance.barHeight + 28
    focus: true
    Component.onCompleted: {
        sync();
        forceActiveFocus();
    }
    Keys.onEscapePressed: closeRequested()
    onUnreadOnlyChanged: sync()
    onByAppChanged: sync()
    onExpandedGroupsChanged: sync()
    onBackendChanged: sync()
    function sync() {
        if (!backend)
            return;
        const next = backend.presentation(unreadOnly, byApp, expandedGroups);
        for (let i = 0; i < next.length; ++i) {
            let existing = i;
            while (existing < rows.count && rows.get(existing).key !== next[i].key)
                ++existing;
            if (existing === rows.count)
                rows.insert(i, next[i]);
            else {
                if (existing !== i)
                    rows.move(existing, i, 1);
                rows.setProperty(i, "payload", next[i].payload);
            }
        }
        if (rows.count > next.length)
            rows.remove(next.length, rows.count - next.length);
    }
    Connections {
        target: root.backend
        function onEntriesChanged() {
            root.sync();
        }
    }
    Timer {
        interval: 60000
        repeat: true
        running: root.visible
        onTriggered: root.sync()
    }
    ListModel {
        id: rows
        dynamicRoles: true
    }
    ShellSurface {
        anchors.fill: parent
        accented: true
        railT: root.railT
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 33
            Layout.bottomMargin: 21
            ColumnLayout {
                Layout.fillWidth: false
                spacing: 7
                NotificationLabel {
                    text: qsTr("YOUR INBOX")
                    font.pixelSize: 8
                    font.letterSpacing: 2
                }
                RowLayout {
                    spacing: 13
                    NotificationLabel {
                        text: qsTr("Notifications")
                        color: Appearance.text
                        font.pixelSize: 23
                        font.weight: Font.Medium
                        font.letterSpacing: -0.7
                    }
                    NotificationLabel {
                        text: String(root.total).padStart(2, "0")
                        color: Appearance.stops[2]
                        font.family: Tokens.font_family
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignBaseline
                    }
                }
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                implicitWidth: 28
                implicitHeight: 28
                iconName: "window-close"
                label: qsTr("Close notification center")
                flat: true
                foreground: Appearance.muted
                onClicked: root.closeRequested()
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 22
            Layout.rightMargin: 22
            implicitHeight: 61
            radius: Appearance.radius * 0.65
            color: Qt.alpha(Appearance.card, 0.45)
            border.color: quiet.checked ? Qt.tint(Appearance.outline, Qt.alpha(Appearance.stops[2], 0.42)) : Appearance.outline
            RowLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12
                ShellIcon {
                    source: "weather-clear-night"
                    color: quiet.checked ? Appearance.stops[2] : Appearance.muted
                    isMask: true
                    Layout.preferredWidth: 25
                    Layout.preferredHeight: 20
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 5
                    NotificationLabel {
                        text: qsTr("Do not disturb")
                        color: Appearance.text
                        font.pixelSize: 11
                        font.weight: Font.Medium
                    }
                    NotificationLabel {
                        text: quiet.checked ? qsTr("On · quietly saving incoming notifications") : qsTr("Off · showing incoming notifications")
                        font.pixelSize: 9
                        Layout.fillWidth: true
                    }
                }
                Switch {
                    id: quiet
                    objectName: "notificationDnd"
                    implicitWidth: 33
                    implicitHeight: 20
                    padding: 0
                    checked: root.backend ? root.backend.doNotDisturb : false
                    enabled: root.available
                    Accessible.name: qsTr("Do not disturb")
                    onToggled: if (root.backend)
                        root.backend.doNotDisturb = checked
                    indicator: Rectangle {
                        width: 33
                        height: 20
                        radius: 12
                        color: quiet.checked ? Appearance.stops[2] : Appearance.recess
                        border.color: quiet.visualFocus ? Appearance.text : Appearance.outline
                        Rectangle {
                            x: quiet.checked ? 17 : 4
                            y: 4
                            width: 12
                            height: 12
                            radius: 6
                            color: quiet.checked ? Appearance.surface : Appearance.muted
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.topMargin: 17
            Layout.bottomMargin: 13
            spacing: 6
            ShellButton {
                text: qsTr("All  %1").arg(root.total)
                flat: root.unreadOnly
                implicitHeight: 30
                onClicked: root.unreadOnly = false
                Accessible.checkable: true
                Accessible.checked: !root.unreadOnly
            }
            ShellButton {
                objectName: "notificationUnreadFilter"
                text: qsTr("Unread  %1").arg(root.unread)
                flat: !root.unreadOnly
                implicitHeight: 30
                onClicked: root.unreadOnly = true
                Accessible.checkable: true
                Accessible.checked: root.unreadOnly
            }
            Item {
                Layout.fillWidth: true
            }
            ShellButton {
                objectName: "notificationMarkAllRead"
                iconName: "checkmark"
                label: qsTr("Mark all as read")
                implicitWidth: 28
                implicitHeight: 28
                flat: true
                enabled: root.unread > 0
                foreground: Appearance.muted
                onClicked: root.backend.markAllRead()
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ListView {
                id: inbox
                objectName: "notificationHistory"
                anchors.fill: parent
                anchors.leftMargin: 23
                anchors.rightMargin: 23
                clip: true
                spacing: 7
                boundsBehavior: Flickable.StopAtBounds
                model: rows
                cacheBuffer: 1200
                ScrollBar.vertical: ScrollBar {
                    width: 4
                }
                footer: Item {
                    height: 20
                }
                delegate: Loader {
                    id: row
                    required property string kind
                    required property var payload
                    width: inbox.width
                    sourceComponent: kind === "notification" ? cardComponent : kind === "section" ? sectionComponent : kind === "group" ? groupComponent : earlierComponent
                    Component {
                        id: cardComponent
                        NotificationCard {
                            notification: row.payload
                            showApp: !root.byApp
                            backend: root.backend
                            previews: root.previews
                            width: row.width
                            onDismissRequested: root.backend.dismiss(row.payload.id)
                            onPauseRequested: paused => root.backend.setExpiryPaused(row.payload.id, paused)
                        }
                    }
                    Component {
                        id: sectionComponent
                        RowLayout {
                            width: row.width
                            height: row.payload.label === qsTr("Needs attention") ? 22 : 30
                            spacing: 12
                            NotificationLabel {
                                text: row.payload.label
                                font.pixelSize: 9
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 1
                                color: Appearance.outline
                            }
                        }
                    }
                    Component {
                        id: groupComponent
                        RowLayout {
                            width: row.width
                            height: 43
                            spacing: 9
                            Rectangle {
                                Layout.preferredWidth: 25
                                Layout.preferredHeight: 25
                                radius: 8
                                color: Qt.alpha(Appearance.stops[row.payload.colorIndex], 0.1)
                                border.color: Qt.alpha(Appearance.stops[row.payload.colorIndex], 0.3)
                                ShellIcon {
                                    anchors.centerIn: parent
                                    width: 15
                                    height: 15
                                    source: row.payload.appIcon || "notifications"
                                    isMask: !row.payload.appIcon
                                    color: Appearance.stops[row.payload.colorIndex]
                                }
                            }
                            NotificationLabel {
                                text: row.payload.appName
                                color: Appearance.text
                                font.pixelSize: 11
                            }
                            NotificationLabel {
                                text: row.payload.count
                                font.pixelSize: 9
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            ShellButton {
                                implicitWidth: 28
                                implicitHeight: 28
                                iconName: "window-close"
                                label: qsTr("Clear notifications from %1").arg(row.payload.appName)
                                foreground: Appearance.muted
                                flat: true
                                onClicked: root.backend.clearGroup(row.payload.appKey)
                            }
                        }
                    }
                    Component {
                        id: earlierComponent
                        Item {
                            width: row.width
                            height: 21
                            ShellButton {
                                y: -7
                                height: 28
                                width: parent.width - 14
                                x: 7
                                text: root.expandedGroups.indexOf(row.payload.groupKey) >= 0 ? qsTr("Show fewer notifications") : qsTr("%1 earlier notifications").arg(row.payload.count - 1)
                                iconName: root.expandedGroups.indexOf(row.payload.groupKey) >= 0 ? "arrow-up" : "arrow-down"
                                labelSize: 9
                                foreground: Appearance.muted
                                flat: true
                                outlined: true
                                onClicked: {
                                    const key = row.payload.groupKey;
                                    root.expandedGroups = root.expandedGroups.indexOf(key) >= 0 ? root.expandedGroups.filter(value => value !== key) : root.expandedGroups.concat([key]);
                                }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                visible: rows.count === 0
                anchors.centerIn: parent
                width: parent.width - 70
                spacing: 13
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 94
                    Layout.preferredHeight: 90
                    Repeater {
                        model: 3
                        Rectangle {
                            required property int index
                            x: 14 + index * 9
                            y: 15 + index * 8
                            width: 47
                            height: 57
                            rotation: (index - 1) * 14
                            radius: 10
                            color: Qt.alpha(Appearance.card, 0.6)
                            border.color: Qt.alpha(Appearance.stops[index], 0.35)
                        }
                    }
                    ShellIcon {
                        anchors.centerIn: parent
                        width: 21
                        height: 21
                        source: "checkmark"
                        color: Appearance.stops[2]
                        isMask: true
                    }
                }
                NotificationLabel {
                    Layout.fillWidth: true
                    Layout.maximumWidth: root.width - 70
                    text: !root.available ? qsTr("Notifications are handled elsewhere") : root.unreadOnly && root.total ? qsTr("You’re all caught up.") : qsTr("A little room to focus.")
                    color: Appearance.text
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }
                NotificationLabel {
                    Layout.fillWidth: true
                    Layout.maximumWidth: root.width - 70
                    text: !root.available ? qsTr("Another notification daemon is running in this session.") : root.unreadOnly && root.total ? qsTr("Your read notifications are still in All.") : qsTr("New notifications will find a home here. For now, enjoy the quiet.")
                    font.pixelSize: 11
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    lineHeight: 1.7
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Appearance.outline
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 23
            Layout.rightMargin: 18
            Layout.preferredHeight: 56
            spacing: 8
            ShellIcon {
                source: "notifications"
                isMask: true
                color: Appearance.muted
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
            }
            NotificationLabel {
                Layout.fillWidth: true
                text: qsTr("%1 unread · %2 total").arg(root.unread).arg(root.total)
                font.pixelSize: 9
            }
            ShellButton {
                objectName: "notificationUndo"
                visible: root.backend ? root.backend.canUndo : false
                text: qsTr("Undo clear")
                flat: true
                foreground: Appearance.stops[2]
                labelSize: 10
                onClicked: root.backend.undoClear()
            }
            ShellButton {
                objectName: "notificationClearAll"
                visible: root.total > 0
                text: qsTr("Clear all")
                iconName: "edit-delete"
                flat: true
                labelSize: 10
                onClicked: root.backend.clear()
            }
        }
    }
}
