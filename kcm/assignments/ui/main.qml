// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kcmutils as KCMUtils
import org.kde.kirigami as Kirigami

KCMUtils.SimpleKCM {
    id: root

    // Capture the context property so child components can access it via root.kcmModule
    readonly property var kcmModule: kcm
    // View mode: 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    readonly property int viewMode: root.kcmModule.autotileEnabled ? root.kcmModule.assignmentViewMode : 0

    topPadding: Kirigami.Units.largeSpacing
    bottomPadding: Kirigami.Units.largeSpacing
    leftPadding: Kirigami.Units.largeSpacing
    rightPadding: Kirigami.Units.largeSpacing

    // Inline constants (from monolith Constants object)
    QtObject {
        id: constants

        readonly property real labelSecondaryOpacity: 0.7
        readonly property int quickLayoutSlotCount: 9
    }

    WindowPickerDialog {
        id: windowPickerDialog

        kcm: root.kcmModule
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Assign layouts to monitors, activities, configure quick-switch shortcuts, and set up app-to-zone rules.")
            visible: true
        }

        // Mode selector (visible only when autotiling is enabled)
        Item {
            Layout.fillWidth: true
            implicitHeight: modeSelectorCard.implicitHeight
            visible: root.kcmModule.autotileEnabled

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
                        currentIndex: root.kcmModule.assignmentViewMode
                        onActivated: {
                            root.kcmModule.assignmentViewMode = model[currentIndex].value;
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

        // Monitor Assignments
        Item {
            Layout.fillWidth: true
            implicitHeight: monitorCard.implicitHeight

            MonitorAssignmentsCard {
                id: monitorCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                viewMode: root.viewMode
            }

        }

        // Activity Assignments
        Item {
            Layout.fillWidth: true
            implicitHeight: activityCard.implicitHeight
            visible: root.kcmModule.activitiesAvailable

            ActivityAssignmentsCard {
                id: activityCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                viewMode: root.viewMode
            }

        }

        // Info when Activities not available
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: !root.kcmModule.activitiesAvailable && root.kcmModule.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based layout assignments require the KDE Activities service to be running.")
        }

        // App-to-zone rules
        Item {
            Layout.fillWidth: true
            implicitHeight: appRulesCard.implicitHeight

            AppRulesCard {
                id: appRulesCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                windowPickerDialog: windowPickerDialog
                viewMode: root.viewMode
            }

        }

        // Quick Layout Shortcuts
        Item {
            Layout.fillWidth: true
            implicitHeight: quickSlotsCard.implicitHeight

            QuickLayoutSlotsCard {
                id: quickSlotsCard

                anchors.fill: parent
                kcm: root.kcmModule
                constants: constants
                viewMode: root.viewMode
            }

        }

    }

}
