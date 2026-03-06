// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import ".."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Assignments tab - Monitor, activity, quick layout slot, and app-to-zone assignments
 *
 * When autotiling is enabled, a mode dropdown appears at the top allowing
 * the user to switch between Snapping and Tiling configurations.
 * Both modes persist independently. When autotiling is off, the tab
 * works as before (snapping only, no dropdown).
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants
    // View mode: 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    // When autotiling is disabled, this is always 0.
    readonly property int viewMode: root.kcm.autotileEnabled ? root.kcm.assignmentViewMode : 0

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
            text: i18n("Assign layouts to monitors, activities, configure quick-switch shortcuts, and set up app-to-zone rules.")
            visible: true
        }

        // ═══════════════════════════════════════════════════════════════════
        // MODE SELECTOR (visible only when autotiling is enabled)
        // Follows the same Card + ComboBox pattern used in the Snapping and Tiling tabs
        // ═══════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: modeSelectorCard.implicitHeight
            visible: root.kcm.autotileEnabled

            Kirigami.Card {
                id: modeSelectorCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Configuration Mode")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    WideComboBox {
                        id: modeCombo

                        Layout.fillWidth: true
                        model: [{
                            "text": i18n("Snapping — Zone layouts"),
                            "value": 0
                        }, {
                            "text": i18n("Tiling — Autotile algorithms"),
                            "value": 1
                        }]
                        textRole: "text"
                        valueRole: "value"
                        currentIndex: root.kcm.assignmentViewMode
                        onActivated: {
                            root.kcm.assignmentViewMode = model[currentIndex].value;
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("Switch between snapping and tiling configurations. Both are saved independently.")
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        Layout.margins: Kirigami.Units.smallSpacing
                        Layout.topMargin: Kirigami.Units.smallSpacing * 2
                        visible: true
                        type: root.viewMode === 1 ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information
                        text: root.viewMode === 1 ? i18n("Tiling mode: assign autotile algorithms to each monitor. These are used when tiling is active.") : i18n("Snapping mode: assign zone layouts to each monitor. These are used when dragging windows.")
                    }

                }

            }

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
                viewMode: root.viewMode
            }

        }

        // Activity Assignments (visible when Activities are available in both modes)
        Item {
            Layout.fillWidth: true
            implicitHeight: activityCard.implicitHeight
            visible: root.kcm.activitiesAvailable

            ActivityAssignmentsCard {
                id: activityCard

                anchors.fill: parent
                kcm: root.kcm
                constants: root.constants
                viewMode: root.viewMode
            }

        }

        // Info message when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !root.kcm.activitiesAvailable && root.kcm.screens.length > 0
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
                viewMode: root.viewMode
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
                viewMode: root.viewMode
            }

        }

    }

}
