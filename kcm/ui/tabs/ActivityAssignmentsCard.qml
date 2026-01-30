// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Activity assignments card - Assign layouts to KDE Activities
 *
 * Refactored to use AssignmentRow component, eliminating duplicated patterns.
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
            text: i18n("Assign layouts to KDE Activities. Layout changes automatically when you switch activities.")
            wrapMode: Text.WordWrap
            opacity: root.constants.labelSecondaryOpacity
        }

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
                id: activityDelegate
                width: ListView.view.width
                height: activityContent.implicitHeight + Kirigami.Units.smallSpacing * 2
                required property var modelData
                required property int index

                property string activityId: modelData.id || ""
                property string activityName: modelData.name || ""
                property string activityIcon: modelData.icon && modelData.icon !== "" ? modelData.icon : "activities"

                ColumnLayout {
                    id: activityContent
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    spacing: Kirigami.Units.smallSpacing

                    // Activity header
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            source: activityDelegate.activityIcon
                            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                        }

                        Label {
                            Layout.fillWidth: true
                            text: activityDelegate.activityName
                            font.bold: activityDelegate.activityId === root.kcm.currentActivity
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: activityDelegate.activityId === root.kcm.currentActivity
                            text: i18n("Current")
                            font.italic: true
                            opacity: 0.7
                        }
                    }

                    // Per-screen assignments using AssignmentRow
                    Repeater {
                        model: root.kcm.screens

                        AssignmentRow {
                            id: screenRow
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.gridUnit * 2

                            required property var modelData
                            property string screenName: modelData.name || ""

                            kcm: root.kcm
                            iconSource: "video-display"
                            iconOpacity: 0.7
                            labelText: screenName
                            noneText: i18n("Use default")
                            currentLayoutId: {
                                let hasExplicit = root.kcm.hasExplicitAssignmentForScreenActivity(screenName, activityDelegate.activityId)
                                return hasExplicit ? (root.kcm.getLayoutForScreenActivity(screenName, activityDelegate.activityId) || "") : ""
                            }

                            Connections {
                                target: root.kcm
                                function onActivityAssignmentsChanged() {
                                    let hasExplicit = root.kcm.hasExplicitAssignmentForScreenActivity(screenRow.screenName, activityDelegate.activityId)
                                    screenRow.currentLayoutId = hasExplicit ?
                                        (root.kcm.getLayoutForScreenActivity(screenRow.screenName, activityDelegate.activityId) || "") : ""
                                }
                            }

                            onAssignmentSelected: (layoutId) => {
                                root.kcm.assignLayoutToScreenActivity(screenName, activityDelegate.activityId, layoutId)
                            }
                            onAssignmentCleared: {
                                root.kcm.clearScreenActivityAssignment(screenName, activityDelegate.activityId)
                            }
                        }
                    }
                }
            }
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.kcm.activities.length === 0 && root.kcm.activitiesAvailable
            type: Kirigami.MessageType.Information
            text: i18n("No activities found. Create activities in System Settings → Activities.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.kcm.screens.length === 0 && root.kcm.activities.length > 0
            type: Kirigami.MessageType.Warning
            text: i18n("No screens detected. Make sure the PlasmaZones daemon is running.")
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.smallSpacing
        }
    }
}
