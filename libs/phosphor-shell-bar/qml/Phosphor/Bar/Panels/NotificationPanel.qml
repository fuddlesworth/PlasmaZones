// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.NotificationPanel, the notification centre.
//
// Everything the shell's notification server has received, newest first,
// kept after it expires so the panel shows what was missed rather than
// only what is on screen. The retained list lives in the shell's
// NotificationController, bound here as the `NotificationRegistry` context
// property, because a QML-side list would be lost on every reload and
// because the notifications themselves are owned and freed by the server.
//
// A notification the server still holds keeps its actions; one that has
// expired is a record, and its actions are gone because the sending
// application has already been told it closed.

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import Phosphor.Widgets

PanelFrame {
    id: root

    title: qsTr("Notifications")
    iconName: "notifications"
    subtitle: {
        if (!NotificationRegistry.serverActive)
            return qsTr("Another notification daemon is running");
        if (NotificationRegistry.count === 0)
            return qsTr("Nothing new");
        return NotificationRegistry.count === 1 ? qsTr("1 notification") : qsTr("%1 notifications").arg(NotificationRegistry.count);
    }

    // Opening the panel is what "read" means, so the badge clears here
    // rather than on any particular row being looked at.
    Component.onCompleted: NotificationRegistry.markAllRead()

    headerAction: Component {
        PhosphorButton {
            text: qsTr("Clear")
            variant: PhosphorButton.Text
            enabled: NotificationRegistry.count > 0
            onClicked: NotificationRegistry.clear()
        }
    }

    Text {
        width: parent.width
        visible: NotificationRegistry.count === 0
        text: NotificationRegistry.serverActive ? qsTr("No notifications") : qsTr("Notifications are being handled by another program, so none arrive here.")
        color: Theme.on_surface_variant
        font.pixelSize: Tokens.font_size_body_s
        font.family: Tokens.font_family_ui
        wrapMode: Text.Wrap
        topPadding: Tokens.spacing_s
        bottomPadding: Tokens.spacing_s
    }

    Repeater {
        model: NotificationRegistry

        delegate: Item {
            id: entry

            required property int notificationId
            required property string appName
            required property string appIcon
            required property string summary
            required property string body
            required property var timestamp
            required property bool live

            width: parent ? parent.width : 0
            implicitHeight: entryLayout.implicitHeight + Tokens.spacing_s

            RowLayout {
                id: entryLayout

                width: entry.width
                spacing: Tokens.spacing_s

                Kirigami.Icon {
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    Layout.alignment: Qt.AlignTop
                    // The application's own icon when it sent one; the
                    // generic glyph otherwise. Not a mask: an application
                    // icon is a picture, and tinting it to a single colour
                    // would flatten a logo into a silhouette.
                    source: entry.appIcon !== "" ? entry.appIcon : "notifications"
                    isMask: entry.appIcon === ""
                    color: Theme.on_surface_variant
                    // An expired entry is a record rather than something
                    // still asking for attention, so it sits back.
                    opacity: entry.live ? 1 : StateLayer.disabled_content
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.spacing_xs

                        Text {
                            Layout.fillWidth: true
                            text: entry.summary
                            color: Theme.on_surface
                            font.pixelSize: Tokens.font_size_body_m
                            font.family: Tokens.font_family_ui
                            font.weight: Tokens.font_weight_medium
                            elide: Text.ElideRight
                        }

                        Text {
                            text: Qt.formatTime(entry.timestamp, Qt.locale().timeFormat(Locale.ShortFormat))
                            color: Theme.on_surface_variant
                            font.pixelSize: Tokens.font_size_label_s
                            font.family: Tokens.font_family
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: entry.body !== ""
                        text: entry.body
                        color: Theme.on_surface_variant
                        font.pixelSize: Tokens.font_size_label_s
                        font.family: Tokens.font_family_ui
                        wrapMode: Text.Wrap
                        // Two lines, then elide. A notification body can be
                        // arbitrarily long and one of them must not push
                        // every other entry off the panel.
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: entry.appName !== ""
                        text: entry.appName
                        color: Theme.on_surface_variant
                        font.pixelSize: Tokens.font_size_label_s
                        font.family: Tokens.font_family_ui
                        opacity: 0.7
                        elide: Text.ElideRight
                    }
                }

                BarIconButton {
                    Layout.alignment: Qt.AlignTop
                    iconName: "window-close"
                    label: qsTr("Dismiss")
                    onActivated: NotificationRegistry.dismiss(entry.notificationId)
                }
            }
        }
    }
}
