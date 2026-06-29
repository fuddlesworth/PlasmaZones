// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// phosphor-toast-demo, the Phase 3.4 acceptance demo.
//
// A ToastHost overlay fed two ways: real notifications from the
// NotificationServer (so `notify-send` raises a toast when this process
// owns the bus name), and in-window buttons (which always work). A "Do
// Not Disturb" pill exercises the per-app-rules seam.

import Phosphor.Notifications
import Phosphor.Theme
import Phosphor.Widgets
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root

    // A small inline image (no external assets / icon theme needed) to
    // demonstrate Toast image support.
    readonly property url sampleImage: "data:image/svg+xml;utf8,<svg xmlns='http://www.w3.org/2000/svg' width='40' height='40'><rect width='40' height='40' rx='8' fill='%2322D3EE'/><circle cx='20' cy='20' r='9' fill='%230B1730'/></svg>"

    // A do-not-disturb rule object plugged into ToastHost.rules.
    readonly property var dndRule: QtObject {
        function evaluate(toast) {
            return {
                "suppress": true
            };
        }
    }

    width: 720
    height: 560
    visible: true
    title: qsTr("Phosphor Toast Demo")
    color: Theme.background

    // Feed real notifications into the toast stack.
    Connections {
        function onNotificationAdded(notification) {
            const icon = notification.appIcon;
            const isPath = icon && (icon.indexOf("/") === 0 || icon.indexOf(":") > 0);
            // Prefer the decoded inline image (image-data hint), served by the
            // C++ NotificationImageProvider under image://notification/<id>;
            // fall back to an appIcon path when there's no rich image.
            const image = notification.hasImage ? ("image://notification/" + notification.id) : (isPath ? icon : "");
            toastHost.show({
                "appName": notification.appName,
                "summary": notification.summary,
                "body": notification.body,
                "imageSource": image,
                "urgency": notification.urgency,
                // freedesktop: 0 = never expire (sticky), >0 = that many ms,
                // -1 = "server decides". 0 and explicit values pass through;
                // for -1 the server keeps Critical sticky and otherwise falls
                // back to the default, so mirror that here.
                "timeout": notification.expireTimeout >= 0 ? notification.expireTimeout : (notification.urgency === 2 ? 0 : toastHost.defaultTimeout)
            });
        }

        target: notifier
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_xl
        spacing: Tokens.spacing_l

        Label {
            text: qsTr("Notification Toasts")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_display_s
            font.weight: Tokens.font_weight_demibold
        }

        Label {
            Layout.fillWidth: true
            text: notifier.nameAcquired ? qsTr("Active notification server. Try notify-send 'Build finished' 'All tests passed', or use the buttons.") : qsTr("Another notification daemon owns the bus, so notify-send won't reach this demo. The buttons below still drive the toast stack.")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_m
            wrapMode: Text.WordWrap
        }

        Flow {
            Layout.fillWidth: true
            spacing: Tokens.spacing_m

            PhosphorButton {
                text: qsTr("Info")
                variant: PhosphorButton.Tonal
                onClicked: toastHost.show({
                    "appName": "Demo",
                    "summary": qsTr("Build finished"),
                    "body": qsTr("All <b>42</b> tests passed."),
                    "urgency": 1
                })
            }

            PhosphorButton {
                text: qsTr("With image")
                variant: PhosphorButton.Tonal
                onClicked: toastHost.show({
                    "appName": "Photos",
                    "summary": qsTr("New photo added"),
                    "body": qsTr("Tap to open in the gallery."),
                    "imageSource": root.sampleImage,
                    "urgency": 1
                })
            }

            PhosphorButton {
                text: qsTr("Critical")
                variant: PhosphorButton.Filled
                onClicked: toastHost.show({
                    "appName": "Power",
                    "summary": qsTr("Battery low"),
                    "body": qsTr("3% remaining. Plug in soon."),
                    "urgency": 2,
                    "timeout": 0
                })
            }

            PhosphorButton {
                text: qsTr("Burst ×5")
                variant: PhosphorButton.Outlined
                onClicked: {
                    for (let i = 1; i <= 5; ++i)
                        toastHost.show({
                            "appName": "Chat",
                            "summary": qsTr("Message %1").arg(i),
                            "body": qsTr("Queues past maxVisible."),
                            "urgency": 1
                        });
                }
            }

            PhosphorPill {
                text: qsTr("Do Not Disturb")
                selected: toastHost.rules !== null
                onToggled: toastHost.rules = toastHost.rules ? null : root.dndRule
            }
        }

        Item {
            Layout.fillHeight: true
        }
    }

    // The toast overlay sits on top, stacking from the top-right.
    ToastHost {
        id: toastHost

        anchors.fill: parent
        maxVisible: 4
    }
}
