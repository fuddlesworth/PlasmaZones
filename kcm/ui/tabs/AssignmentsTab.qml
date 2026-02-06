// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Assignments tab - Monitor, activity, quick layout slot, and app-to-zone assignments
 *
 * This tab is composed of four card components:
 * - MonitorAssignmentsCard: Per-monitor and per-desktop layout assignments
 * - ActivityAssignmentsCard: Per-activity layout assignments (if Activities available)
 * - AppRulesCard: Per-layout app-to-zone auto-snap rules
 * - QuickLayoutSlotsCard: Keyboard shortcut to layout mappings
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    clip: true
    contentWidth: availableWidth

    WindowPickerDialog {
        id: windowPickerDialog
        kcm: root.kcm
    }

    ColumnLayout {
        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign layouts to monitors, configure quick-switch shortcuts, and set up app-to-zone rules.")
            visible: true
        }

        // Monitor Assignments - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: monitorCard.implicitHeight

            MonitorAssignmentsCard {
                id: monitorCard
                anchors.fill: parent
                kcm: root.kcm
                constants: root.constants
            }
        }

        // Activity Assignments (only visible when Activities are available)
        Item {
            Layout.fillWidth: true
            implicitHeight: activityCard.implicitHeight
            visible: root.kcm.activitiesAvailable

            ActivityAssignmentsCard {
                id: activityCard
                anchors.fill: parent
                kcm: root.kcm
                constants: root.constants
            }
        }

        // Info message when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !root.kcm.activitiesAvailable
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based layout assignments require the KDE Activities service to be running.")
        }

        // App-to-zone rules - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: appRulesCard.implicitHeight

            AppRulesCard {
                id: appRulesCard
                anchors.fill: parent
                kcm: root.kcm
                constants: root.constants
                windowPickerDialog: windowPickerDialog
            }
        }

        // Quick Layout Shortcuts - wrapped in Item for stable sizing
        Item {
            Layout.fillWidth: true
            implicitHeight: quickSlotsCard.implicitHeight

            QuickLayoutSlotsCard {
                id: quickSlotsCard
                anchors.fill: parent
                kcm: root.kcm
                constants: root.constants
            }
        }

    }
}
