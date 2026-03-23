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
SettingsCard {
    id: root

    required property var appSettings
    // 0 = snapping (zone layouts), 1 = tiling (autotile algorithms)
    property int viewMode: 0
    property int _lockRevision: 0

    function getScreenLayout(screenName) {
        return viewMode === 1 ? (appSettings.getTilingLayoutForScreen(screenName) || "") : (appSettings.getLayoutForScreen(screenName) || "");
    }

    function getScreenActivityLayout(screenName, activityId) {
        if (viewMode === 1)
            return appSettings.hasExplicitTilingAssignmentForScreenActivity(screenName, activityId) ? (appSettings.getTilingLayoutForScreenActivity(screenName, activityId) || "") : "";
        else
            return appSettings.hasExplicitAssignmentForScreenActivity(screenName, activityId) ? (appSettings.getSnappingLayoutForScreenActivity(screenName, activityId) || "") : "";
    }

    function assignScreenActivity(screenName, activityId, layoutId) {
        if (viewMode === 1)
            appSettings.assignTilingLayoutToScreenActivity(screenName, activityId, layoutId);
        else
            appSettings.assignLayoutToScreenActivity(screenName, activityId, layoutId);
    }

    function clearScreenActivity(screenName, activityId) {
        if (viewMode === 1)
            appSettings.clearTilingScreenActivityAssignment(screenName, activityId);
        else
            appSettings.clearScreenActivityAssignment(screenName, activityId);
    }

    visible: appSettings.activitiesAvailable
    headerText: root.viewMode === 1 ? i18n("Activity Tiling Assignments") : i18n("Activity Assignments")
    collapsible: true

    Connections {
        function onLockedScreensChanged() {
            root._lockRevision++;
        }

        target: root.appSettings
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            text: root.viewMode === 1 ? i18n("Assign tiling algorithms to KDE Activities. Algorithm changes automatically when you switch activities.") : i18n("Assign layouts to KDE Activities. Layout changes automatically when you switch activities.")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        ListView {
            id: activitiesListView

            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            Layout.margins: Kirigami.Units.smallSpacing
            clip: true
            model: root.appSettings.activities
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
                            font.weight: activityDelegate.activityId === root.appSettings.currentActivity ? Font.DemiBold : Font.Normal
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: activityDelegate.activityId === root.appSettings.currentActivity
                            text: i18n("Current")
                            font.italic: true
                            opacity: 0.7
                        }

                    }

                    // Per-screen assignments using AssignmentRow + lock toggle
                    Repeater {
                        model: root.appSettings.screens

                        RowLayout {
                            id: activityScreenRow

                            required property var modelData
                            required property int index
                            property string screenName: modelData.name || ""

                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.gridUnit * 2
                            spacing: Kirigami.Units.smallSpacing

                            AssignmentRow {
                                id: screenRow

                                property string monitorLayout: {
                                    void (activityDelegate._activityRevision);
                                    return root.getScreenLayout(activityScreenRow.screenName);
                                }

                                Layout.fillWidth: true
                                enabled: {
                                    void (activityDelegate._activityRevision);
                                    void (root._lockRevision);
                                    return !root.appSettings.isContextLocked(activityScreenRow.screenName, 0, activityDelegate.activityId, root.viewMode);
                                }
                                appSettings: root.appSettings
                                iconSource: "video-display"
                                iconOpacity: 0.7
                                layoutFilter: root.viewMode === 1 ? 1 : 0
                                showPreview: true
                                labelText: {
                                    let mfr = activityScreenRow.modelData.manufacturer || "";
                                    let mdl = activityScreenRow.modelData.model || "";
                                    let parts = [mfr, mdl].filter(function(s) {
                                        return s !== "";
                                    });
                                    let displayInfo = parts.join(" ");
                                    return displayInfo ? activityScreenRow.screenName + " — " + displayInfo : activityScreenRow.screenName;
                                }
                                noneText: i18n("Use default")
                                resolvedDefaultId: monitorLayout !== "" ? monitorLayout : (root.appSettings.defaultLayoutId || "")
                                currentLayoutId: {
                                    void (activityDelegate._activityRevision);
                                    return root.getScreenActivityLayout(activityScreenRow.screenName, activityDelegate.activityId);
                                }
                                onAssignmentSelected: (layoutId) => {
                                    root.assignScreenActivity(activityScreenRow.screenName, activityDelegate.activityId, layoutId);
                                }
                                onAssignmentCleared: {
                                    root.clearScreenActivity(activityScreenRow.screenName, activityDelegate.activityId);
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

                                    function onLockedScreensChanged() {
                                        activityDelegate._activityRevision++;
                                    }

                                    target: root.appSettings
                                }

                            }

                            ToolButton {
                                icon.name: {
                                    void (activityDelegate._activityRevision);
                                    void (root._lockRevision);
                                    return root.appSettings.isContextLocked(activityScreenRow.screenName, 0, activityDelegate.activityId, root.viewMode) ? "object-locked" : "object-unlocked";
                                }
                                opacity: {
                                    void (activityDelegate._activityRevision);
                                    void (root._lockRevision);
                                    return root.appSettings.isContextLocked(activityScreenRow.screenName, 0, activityDelegate.activityId, root.viewMode) ? 1 : 0.4;
                                }
                                display: ToolButton.IconOnly
                                ToolTip.text: {
                                    void (activityDelegate._activityRevision);
                                    void (root._lockRevision);
                                    return root.appSettings.isContextLocked(activityScreenRow.screenName, 0, activityDelegate.activityId, root.viewMode) ? i18n("Unlock layout for %1 on %2", activityDelegate.activityName, activityScreenRow.screenName) : i18n("Lock layout for %1 on %2", activityDelegate.activityName, activityScreenRow.screenName);
                                }
                                ToolTip.visible: hovered
                                onClicked: root.appSettings.toggleContextLock(activityScreenRow.screenName, 0, activityDelegate.activityId, root.viewMode)
                            }

                        }

                    }

                }

            }

        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.appSettings.activities.length === 0 && root.appSettings.activitiesAvailable
            type: Kirigami.MessageType.Information
            text: i18n("No activities found. Create activities in System Settings → Activities.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            visible: root.appSettings.screens.length === 0 && root.appSettings.activities.length > 0
            type: Kirigami.MessageType.Warning
            text: i18n("No screens detected. Make sure the PlasmaZones daemon is running.")
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Kirigami.Units.smallSpacing
        }

    }

}
