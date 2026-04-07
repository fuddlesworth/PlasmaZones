// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Notification system for the layout editor
 *
 * Provides success and error notification banners with auto-dismiss.
 * Extracted from EditorWindow.qml to reduce file size.
 */
Item {
    id: notifications

    // Reference to anchor to (typically topBar.bottom)
    required property Item anchorItem
    // Parent window width (passed from parent for sizing)
    property real windowWidth: parent ? parent.width : 0

    // Public functions to show notifications
    function showSuccess(message) {
        successNotification.show(message);
    }

    function showError(message) {
        errorNotification.show(message);
    }

    // Constants for visual styling
    QtObject {
        id: constants

        readonly property int notificationBorderWidth: Kirigami.Units.smallSpacing / 2 // 2px - standard border width
    }

    // Success notification component
    Rectangle {
        id: successNotification

        property alias text: successNotificationLabel.text

        function show(message) {
            successNotificationLabel.text = message;
            successHideAnim.stop();
            successShowAnim.start();
            successNotificationTimer.restart();
        }

        function hide() {
            successShowAnim.stop();
            successHideAnim.start();
        }

        anchors.horizontalCenter: parent.horizontalCenter
        y: anchorItem ? (anchorItem.y + anchorItem.height + Kirigami.Units.gridUnit * 2) : Kirigami.Units.gridUnit * 2
        width: Math.min(Kirigami.Units.gridUnit * 50, windowWidth * 0.8)
        height: successNotificationContent.implicitHeight + Kirigami.Units.gridUnit * 2
        visible: opacity > 0
        opacity: 0
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
        border.color: Kirigami.Theme.positiveTextColor
        border.width: constants.notificationBorderWidth
        radius: Kirigami.Units.smallSpacing * 1.5
        z: 200
        Accessible.name: i18nc("@info:accessibility", "Success notification")
        Accessible.description: successNotificationLabel.text || ""
        Accessible.role: Accessible.AlertMessage

        ColumnLayout {
            id: successNotificationContent

            anchors.fill: parent
            anchors.margins: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.gridUnit

                Kirigami.Icon {
                    source: "dialog-ok-apply"
                    width: Kirigami.Units.iconSizes.medium
                    height: Kirigami.Units.iconSizes.medium
                    color: Kirigami.Theme.positiveTextColor
                }

                Label {
                    id: successNotificationLabel

                    Layout.fillWidth: true
                    text: ""
                    color: Kirigami.Theme.textColor
                    wrapMode: Text.WordWrap
                }

            }

        }

        Timer {
            id: successNotificationTimer

            interval: 3000 // Show for 3 seconds
            onTriggered: successNotification.hide()
        }

        ParallelAnimation {
            id: successShowAnim

            NumberAnimation {
                target: successNotification
                property: "opacity"
                to: 1
                duration: 200
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: successTranslate
                property: "y"
                to: 0
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

        ParallelAnimation {
            id: successHideAnim

            NumberAnimation {
                target: successNotification
                property: "opacity"
                to: 0
                duration: 200
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: successTranslate
                property: "y"
                to: Kirigami.Units.smallSpacing
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

        transform: Translate {
            id: successTranslate

            y: Kirigami.Units.smallSpacing
        }

    }

    // Error notification component
    Rectangle {
        id: errorNotification

        property alias text: errorNotificationLabel.text

        function show(message) {
            errorNotificationLabel.text = message;
            errorHideAnim.stop();
            errorShowAnim.start();
            errorNotificationTimer.restart();
        }

        function hide() {
            errorShowAnim.stop();
            errorHideAnim.start();
        }

        anchors.horizontalCenter: parent.horizontalCenter
        y: anchorItem ? (anchorItem.y + anchorItem.height + Kirigami.Units.gridUnit * 2) : Kirigami.Units.gridUnit * 2
        width: Math.min(Kirigami.Units.gridUnit * 50, windowWidth * 0.8)
        height: errorNotificationContent.implicitHeight + Kirigami.Units.gridUnit * 2
        visible: opacity > 0
        opacity: 0
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.95)
        border.color: Kirigami.Theme.negativeTextColor
        border.width: constants.notificationBorderWidth
        radius: Kirigami.Units.smallSpacing * 1.5
        z: 200
        Accessible.name: i18nc("@info:accessibility", "Error notification")
        Accessible.description: errorNotificationLabel.text || ""
        Accessible.role: Accessible.AlertMessage

        ColumnLayout {
            id: errorNotificationContent

            anchors.fill: parent
            anchors.margins: Kirigami.Units.gridUnit
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.gridUnit

                Kirigami.Icon {
                    source: "dialog-error"
                    width: Kirigami.Units.iconSizes.medium
                    height: Kirigami.Units.iconSizes.medium
                    color: Kirigami.Theme.negativeTextColor
                }

                Label {
                    id: errorNotificationLabel

                    Layout.fillWidth: true
                    text: ""
                    color: Kirigami.Theme.textColor
                    wrapMode: Text.WordWrap
                }

                ToolButton {
                    icon.name: "window-close"
                    onClicked: {
                        errorNotificationTimer.stop();
                        errorNotification.hide();
                    }
                    ToolTip.text: i18nc("@tooltip", "Dismiss")
                    ToolTip.visible: hovered
                    Accessible.name: i18nc("@action:button", "Dismiss notification")
                    Accessible.role: Accessible.Button
                }

            }

        }

        Timer {
            id: errorNotificationTimer

            interval: 5000 // Show for 5 seconds (errors stay longer)
            onTriggered: errorNotification.hide()
        }

        ParallelAnimation {
            id: errorShowAnim

            NumberAnimation {
                target: errorNotification
                property: "opacity"
                to: 1
                duration: 200
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: errorTranslate
                property: "y"
                to: 0
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

        ParallelAnimation {
            id: errorHideAnim

            NumberAnimation {
                target: errorNotification
                property: "opacity"
                to: 0
                duration: 200
                easing.type: Easing.OutCubic
            }

            NumberAnimation {
                target: errorTranslate
                property: "y"
                to: Kirigami.Units.smallSpacing
                duration: 200
                easing.type: Easing.OutCubic
            }

        }

        transform: Translate {
            id: errorTranslate

            y: Kirigami.Units.smallSpacing
        }

    }

}
