// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Activity assignments card - Assign layouts to KDE Activities
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants

    visible: kcm.activitiesAvailable

    header: Kirigami.Heading {
        level: 3
        text: i18n("Activity Assignments")
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            text: i18n("Assign different layouts to KDE Activities. When you switch activities, the layout will change automatically.")
            wrapMode: Text.WordWrap
            opacity: root.constants.labelSecondaryOpacity
        }

        // Activities list
        ListView {
            id: activitiesListView
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.kcm.activities
            interactive: false

            Accessible.name: i18n("Activities list")
            Accessible.role: Accessible.List

            delegate: Item {
                width: ListView.view.width
                height: activityRowLayout.implicitHeight + Kirigami.Units.smallSpacing * 2
                required property var modelData
                required property int index

                property string activityId: modelData.id || ""
                property string activityName: modelData.name || ""
                property string activityIcon: modelData.icon && modelData.icon !== "" ? modelData.icon : "activities"

                ColumnLayout {
                    id: activityRowLayout
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Activity header row
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: activityIcon
                            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                        }

                        Label {
                            Layout.fillWidth: true
                            text: activityName
                            font.bold: activityId === root.kcm.currentActivity
                            elide: Text.ElideRight
                        }

                        // Show "Current" badge if this is the current activity
                        Label {
                            visible: activityId === root.kcm.currentActivity
                            text: i18n("Current")
                            font.italic: true
                            opacity: 0.7
                        }
                    }

                    // Per-screen layout assignments for this activity
                    Repeater {
                        model: root.kcm.screens

                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.gridUnit * 2
                            spacing: Kirigami.Units.smallSpacing

                            required property var modelData
                            property string screenName: modelData.name || ""

                            Kirigami.Icon {
                                source: "video-display"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                opacity: 0.7
                            }

                            Label {
                                text: screenName
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                elide: Text.ElideRight
                            }

                            LayoutComboBox {
                                id: activityLayoutCombo
                                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                                kcm: root.kcm
                                noneText: i18n("Use default")

                                function updateFromAssignment() {
                                    let hasExplicit = root.kcm.hasExplicitAssignmentForScreenActivity(screenName, activityId)
                                    if (!hasExplicit) {
                                        currentLayoutId = ""
                                        return
                                    }
                                    currentLayoutId = root.kcm.getLayoutForScreenActivity(screenName, activityId) || ""
                                }

                                Component.onCompleted: updateFromAssignment()

                                Connections {
                                    target: root.kcm
                                    function onActivityAssignmentsChanged() {
                                        activityLayoutCombo.updateFromAssignment()
                                    }
                                }

                                onActivated: {
                                    let selectedValue = model[currentIndex].value
                                    if (selectedValue === "") {
                                        root.kcm.clearScreenActivityAssignment(screenName, activityId)
                                    } else {
                                        root.kcm.assignLayoutToScreenActivity(screenName, activityId, selectedValue)
                                    }
                                }
                            }

                            ToolButton {
                                icon.name: "edit-clear"
                                onClicked: {
                                    root.kcm.clearScreenActivityAssignment(screenName, activityId)
                                    activityLayoutCombo.clearSelection()
                                }
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Clear assignment")
                            }

                            Item { Layout.fillWidth: true }
                        }
                    }
                }
            }
        }

        // Message when no activities
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.kcm.activities.length === 0 && root.kcm.activitiesAvailable
            type: Kirigami.MessageType.Information
            text: i18n("No activities found. Create activities in System Settings → Activities.")
        }

        // Message when no screens
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.kcm.screens.length === 0 && root.kcm.activities.length > 0
            type: Kirigami.MessageType.Warning
            text: i18n("No screens detected. Make sure the PlasmaZones daemon is running.")
        }

        // Bottom spacer
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.smallSpacing
        }
    }
}
