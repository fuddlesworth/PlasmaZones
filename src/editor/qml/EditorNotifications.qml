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

    // Success notification
    NotificationBanner {
        id: successNotification

        accentColor: Kirigami.Theme.positiveTextColor
        iconSource: "dialog-ok-apply"
        dismissTimeout: 3000
        accessibleRoleName: i18nc("@info:accessibility", "Success notification")
        anchors.horizontalCenter: parent.horizontalCenter
        y: notifications.anchorItem ? (notifications.anchorItem.y + notifications.anchorItem.height + Kirigami.Units.gridUnit * 2) : Kirigami.Units.gridUnit * 2
        width: Math.min(Kirigami.Units.gridUnit * 50, notifications.windowWidth * 0.8)
    }

    // Error notification — offset below success banner when both are visible
    NotificationBanner {
        id: errorNotification

        accentColor: Kirigami.Theme.negativeTextColor
        iconSource: "dialog-error"
        dismissTimeout: 5000
        showCloseButton: true
        accessibleRoleName: i18nc("@info:accessibility", "Error notification")
        anchors.horizontalCenter: parent.horizontalCenter
        y: {
            let baseY = notifications.anchorItem ? (notifications.anchorItem.y + notifications.anchorItem.height + Kirigami.Units.gridUnit * 2) : Kirigami.Units.gridUnit * 2;
            if (successNotification.visible)
                return baseY + successNotification.height + Kirigami.Units.smallSpacing;

            return baseY;
        }
        width: Math.min(Kirigami.Units.gridUnit * 50, notifications.windowWidth * 0.8)
    }

}
