// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Assignments tab - Monitor, activity, and quick layout slot assignments
 *
 * This tab is composed of three card components:
 * - MonitorAssignmentsCard: Per-monitor and per-desktop layout assignments
 * - ActivityAssignmentsCard: Per-activity layout assignments (if Activities available)
 * - QuickLayoutSlotsCard: Keyboard shortcut to layout mappings
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign different layouts to each monitor and configure quick-switch keyboard shortcuts.")
            visible: true
        }

        // Monitor Assignments
        MonitorAssignmentsCard {
            Layout.fillWidth: true
            Layout.fillHeight: false
            kcm: root.kcm
            constants: root.constants
        }

        // Activity Assignments (only visible when Activities are available)
        ActivityAssignmentsCard {
            Layout.fillWidth: true
            Layout.fillHeight: false
            kcm: root.kcm
            constants: root.constants
        }

        // Info message when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !root.kcm.activitiesAvailable
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based layout assignments require the KDE Activities service to be running.")
        }

        // Quick Layout Shortcuts
        QuickLayoutSlotsCard {
            Layout.fillWidth: true
            Layout.fillHeight: false
            kcm: root.kcm
            constants: root.constants
        }
    }
}
