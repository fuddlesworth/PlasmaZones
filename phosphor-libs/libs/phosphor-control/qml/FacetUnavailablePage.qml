// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

/**
 * The page FacetHost shows for a module's page while the module is not
 * loaded. Opening the page loads the module first, so this is only seen when
 * the module could not load: it says why, and offers to try again when that
 * can help.
 *
 * PageHost creates it with `controller`, the host's placeholder for the page,
 * which carries `unavailableReason`, `retryable` and `retry()`, and with
 * `settingsApp`, as it creates every module page.
 */
Item {
    id: root

    // Untyped: the placeholder's class is internal to FacetHost, so its
    // properties are only known at run time.
    required property var controller
    required property var settingsApp

    readonly property string reason: root.controller.unavailableReason !== undefined ? root.controller.unavailableReason : ""

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        width: parent.width - Kirigami.Units.gridUnit * 4
        icon.name: "dialog-error"
        text: qsTr("This page is unavailable")
        explanation: root.reason.length > 0 ? qsTr("Its module could not be loaded. %1").arg(root.reason) : qsTr("Its module could not be loaded.")
        helpfulAction: QQC2.Action {
            enabled: root.controller.retryable === true
            text: qsTr("Try Again")
            icon.name: "view-refresh"
            onTriggered: root.controller.retry()
        }
    }
}
