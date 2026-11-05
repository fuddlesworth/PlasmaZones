// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami
import org.phosphor.animation

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

    // Both banners hang off the same anchor and share one width cap, so the
    // two expressions live here rather than being repeated per banner. The
    // error banner starts from baseY and adds the success banner's height
    // when both are up.
    readonly property real baseY: notifications.anchorItem ? (notifications.anchorItem.y + notifications.anchorItem.height + Kirigami.Units.gridUnit * 2) : Kirigami.Units.gridUnit * 2
    readonly property real bannerWidth: Math.min(Kirigami.Units.gridUnit * 50, notifications.windowWidth * 0.8)

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
        y: notifications.baseY
        width: notifications.bannerWidth
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
            if (successNotification.visible)
                return notifications.baseY + successNotification.height + Kirigami.Units.smallSpacing;

            return notifications.baseY;
        }
        width: notifications.bannerWidth

        // Position transition when success banner fades and error slides up.
        // Banners are editor-shell surfaces (osd.show is for the in-shell OSD);
        // a y-slide isn't really a fade, so route through the widget family
        // root for the generic ease-out shape.
        Behavior on y {
            PhosphorMotionAnimation {
                profile: "widget"
            }
        }
    }
}
