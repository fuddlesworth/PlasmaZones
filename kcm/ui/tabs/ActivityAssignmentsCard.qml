// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Activity assignments card - Assign layouts to KDE Activities
 *
 * Refactored to use AssignmentRow component, eliminating duplicated patterns.
 */
Kirigami.Card {
    id: root

    required property var kcm
    required property QtObject constants
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    property int viewMode: 0

    function getScreenLayout(screenName) {
        return viewMode === 1 ? (kcm.getTilingLayoutForScreen(screenName) || "") : (kcm.getLayoutForScreen(screenName) || "");
    }

    function getScreenActivityLayout(screenName, activityId) {
        if (viewMode === 1)
            return kcm.hasExplicitTilingAssignmentForScreenActivity(screenName, activityId) ? (kcm.getTilingLayoutForScreenActivity(screenName, activityId) || "") : "";
        else
            return kcm.hasExplicitAssignmentForScreenActivity(screenName, activityId) ? (kcm.getLayoutForScreenActivity(screenName, activityId) || "") : "";
    }

    function assignScreenActivity(screenName, activityId, layoutId) {
        if (viewMode === 1)
            kcm.assignTilingLayoutToScreenActivity(screenName, activityId, layoutId);
        else
            kcm.assignLayoutToScreenActivity(screenName, activityId, layoutId);
    }

    function clearScreenActivity(screenName, activityId) {
        if (viewMode === 1)
            kcm.clearTilingScreenActivityAssignment(screenName, activityId);
        else
            kcm.clearScreenActivityAssignment(screenName, activityId);
    }

    visible: kcm.activitiesAvailable

    header: Kirigami.Heading {
        level: 3
        text: root.viewMode === 1 ? i18n("Activity Tiling Assignments") : i18n("Activity Assignments")
        padding: Kirigami.Units.smallSpacing
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            text: root.viewMode === 1 ? i18n("Assign tiling algorithms to KDE Activities. Algorithm changes automatically when you switch activities.") : i18n("Assign layouts to KDE Activities. Layout changes automatically when you switch activities.")
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

                required property var modelData
                required property int index
                property string activityId: modelData.id || ""
                property string activityName: modelData.name || ""
                property string activityIcon: modelData.icon && modelData.icon !== "" ? modelData.icon : "activities"
                // Revision counter — incremented when assignment data changes,
                // forcing currentLayoutId bindings to re-evaluate without
                // breaking the binding (imperative assignment breaks bindings).
                property int _activityRevision: 0

                width: ListView.view.width
                height: activityContent.implicitHeight + Kirigami.Units.smallSpacing * 2

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

                            required property var modelData
                            property string screenName: modelData.name || ""
                            // Activity "Default" resolves to monitor's layout (or global if monitor has none)
                            // Uses revision counter to avoid imperative assignment breaking the binding
                            property string monitorLayout: {
                                void (activityDelegate._activityRevision);
                                return root.getScreenLayout(screenRow.screenName);
                            }

                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.gridUnit * 2
                            kcm: root.kcm
                            iconSource: "video-display"
                            iconOpacity: 0.7
                            layoutFilter: root.viewMode === 1 ? 1 : 0
                            showPreview: root.viewMode === 0
                            labelText: {
                                let mfr = modelData.manufacturer || "";
                                let mdl = modelData.model || "";
                                let parts = [mfr, mdl].filter(function(s) {
                                    return s !== "";
                                });
                                let displayInfo = parts.join(" ");
                                return displayInfo ? screenName + " — " + displayInfo : screenName;
                            }
                            noneText: i18n("Use default")
                            resolvedDefaultId: monitorLayout !== "" ? monitorLayout : (root.kcm.defaultLayoutId || "")
                            currentLayoutId: {
                                void (activityDelegate._activityRevision);
                                return root.getScreenActivityLayout(screenRow.screenName, activityDelegate.activityId);
                            }
                            onAssignmentSelected: (layoutId) => {
                                root.assignScreenActivity(screenRow.screenName, activityDelegate.activityId, layoutId);
                            }
                            onAssignmentCleared: {
                                root.clearScreenActivity(screenRow.screenName, activityDelegate.activityId);
                            }

                            Connections {
                                function onActivityAssignmentsChanged() {
                                    activityDelegate._activityRevision++;
                                }

                                function onTilingActivityAssignmentsChanged() {
                                    activityDelegate._activityRevision++;
                                }

                                function onScreenAssignmentsChanged() {
                                    activityDelegate._activityRevision++;
                                }

                                function onTilingScreenAssignmentsChanged() {
                                    activityDelegate._activityRevision++;
                                }

                                target: root.kcm
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
