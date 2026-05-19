// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Assignments sub-page.
 *
 * Gates scrolling mode per monitor / virtual desktop / activity. A screen is
 * put into scroll mode through the Layouts picker (the synthetic "Scrolling"
 * entry); this page is the per-context enable/disable surface — the
 * Scroll{Disabled}Monitors/Desktops/Activities lists — mirroring the disable
 * toggles on the Snapping and Tiling assignment pages, without the layout
 * dropdowns that scroll mode (a single mode, no per-context layout) has no
 * use for. Every toggle routes through ScrollingBridge → SettingsController
 * with the scroll view-mode.
 */
SettingsFlickable {
    id: root

    readonly property var
    settingsBridge: ScrollingBridge {
    }

    contentHeight: mainCol.implicitHeight
    clip: true

    ColumnLayout {
        id: mainCol

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Choose where scrolling mode may run. Assign scrolling to a screen from the Layouts page; these toggles gate it per monitor, virtual desktop, and activity.")
            visible: true
        }

        // ── Monitors ──────────────────────────────────────────────────
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Monitors")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: (root.settingsBridge.screens && root.settingsBridge.screens.length > 0) ? contentHeight : Kirigami.Units.gridUnit * 4
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    interactive: false
                    model: root.settingsBridge.screens
                    Accessible.name: i18n("Monitor list")
                    Accessible.role: Accessible.List

                    Kirigami.PlaceholderMessage {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.gridUnit * 4
                        visible: parent.count === 0
                        text: i18n("No monitors detected")
                        explanation: i18n("Make sure the PlasmaZones daemon is running")
                    }

                    delegate: Item {
                        id: monitorDelegate

                        required property var modelData
                        required property int index
                        property string screenName: modelData.name || ""
                        property bool expanded: false

                        width: ListView.view.width
                        // implicitHeight + both vertical margins — anchors.fill
                        // insets the content by anchors.margins on every side,
                        // so the delegate must reserve top + bottom (matches
                        // the activity delegate below).
                        height: monitorCol.implicitHeight + Kirigami.Units.smallSpacing * 2

                        ColumnLayout {
                            id: monitorCol

                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: "video-display"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            let name = monitorDelegate.modelData.name || i18n("Unknown Monitor");
                                            let mfr = monitorDelegate.modelData.manufacturer || "";
                                            let mdl = monitorDelegate.modelData.model || "";
                                            let parts = [mfr, mdl].filter(function(s) {
                                                return s !== "";
                                            });
                                            let info = parts.join(" ");
                                            return info ? name + " — " + info : name;
                                        }
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: {
                                            let info = monitorDelegate.modelData.resolution || "";
                                            if (monitorDelegate.modelData.isPrimary)
                                                info += (info ? " • " : "") + i18n("Primary");

                                            return info;
                                        }
                                        opacity: 0.7
                                        font: Kirigami.Theme.smallFont
                                        elide: Text.ElideRight
                                    }

                                }

                                Switch {
                                    id: monitorSwitch

                                    property bool monitorActive: !root.settingsBridge.isMonitorDisabled(monitorDelegate.screenName)

                                    checked: monitorActive
                                    Accessible.name: i18n("Scrolling mode on monitor %1", monitorDelegate.screenName)
                                    onToggled: {
                                        root.settingsBridge.setMonitorDisabled(monitorDelegate.screenName, !checked);
                                        monitorActive = checked;
                                    }
                                    ToolTip.visible: hovered
                                    ToolTip.text: checked ? i18n("Disable scrolling mode on this monitor") : i18n("Enable scrolling mode on this monitor")

                                    Connections {
                                        function onDisabledMonitorsChanged() {
                                            monitorSwitch.monitorActive = !root.settingsBridge.isMonitorDisabled(monitorDelegate.screenName);
                                        }

                                        target: root.settingsBridge
                                    }

                                }

                                ToolButton {
                                    visible: root.settingsBridge.virtualDesktopCount > 1
                                    enabled: monitorSwitch.checked
                                    icon.name: monitorDelegate.expanded ? "go-up" : "go-down"
                                    text: monitorDelegate.expanded ? "" : i18n("Per-desktop")
                                    display: AbstractButton.TextBesideIcon
                                    Accessible.name: monitorDelegate.expanded ? i18n("Hide per-desktop toggles") : i18n("Show per-desktop toggles")
                                    onClicked: monitorDelegate.expanded = !monitorDelegate.expanded
                                    ToolTip.visible: hovered
                                    ToolTip.text: monitorDelegate.expanded ? i18n("Hide per-desktop toggles") : i18n("Show per-desktop toggles")
                                }

                            }

                            // Per-desktop toggles (expandable)
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: Kirigami.Units.gridUnit * 2
                                visible: monitorDelegate.expanded && root.settingsBridge.virtualDesktopCount > 1
                                enabled: monitorSwitch.checked
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Separator {
                                    Layout.fillWidth: true
                                }

                                Repeater {
                                    model: root.settingsBridge.virtualDesktopCount

                                    RowLayout {
                                        id: desktopRow

                                        required property int index
                                        property int desktopNumber: index + 1
                                        property string desktopName: root.settingsBridge.virtualDesktopNames[index] || i18n("Desktop %1", desktopNumber)
                                        property bool desktopActive: !root.settingsBridge.isDesktopDisabled(monitorDelegate.screenName, desktopNumber)

                                        Layout.fillWidth: true
                                        spacing: Kirigami.Units.smallSpacing

                                        Kirigami.Icon {
                                            source: "preferences-desktop-virtual"
                                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                            opacity: 0.7
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: desktopRow.desktopName
                                            elide: Text.ElideRight
                                        }

                                        Switch {
                                            checked: desktopRow.desktopActive
                                            Accessible.name: i18n("Scrolling mode on %1", desktopRow.desktopName)
                                            onToggled: {
                                                root.settingsBridge.setDesktopDisabled(monitorDelegate.screenName, desktopRow.desktopNumber, !checked);
                                                desktopRow.desktopActive = checked;
                                            }
                                            ToolTip.visible: hovered
                                            ToolTip.text: checked ? i18n("Disable scrolling mode on %1", desktopRow.desktopName) : i18n("Enable scrolling mode on %1", desktopRow.desktopName)

                                            Connections {
                                                function onDisabledDesktopsChanged() {
                                                    desktopRow.desktopActive = !root.settingsBridge.isDesktopDisabled(monitorDelegate.screenName, desktopRow.desktopNumber);
                                                }

                                                target: root.settingsBridge
                                            }

                                        }

                                    }

                                }

                            }

                        }

                    }

                }

            }

        }

        // ── Activities ────────────────────────────────────────────────
        SettingsCard {
            Layout.fillWidth: true
            visible: root.settingsBridge.activitiesAvailable
            headerText: i18n("Activities")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    text: i18n("Gate scrolling mode per KDE Activity, on each monitor.")
                    wrapMode: Text.WordWrap
                    opacity: 0.7
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: contentHeight
                    Layout.margins: Kirigami.Units.smallSpacing
                    clip: true
                    interactive: false
                    model: root.settingsBridge.activities
                    Accessible.name: i18n("Activities list")
                    Accessible.role: Accessible.List

                    delegate: Item {
                        id: activityDelegate

                        required property var modelData
                        required property int index
                        property string activityId: modelData.id || ""
                        property string activityName: modelData.name || ""

                        width: ListView.view.width
                        height: activityCol.implicitHeight + Kirigami.Units.smallSpacing * 2

                        ColumnLayout {
                            id: activityCol

                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Kirigami.Icon {
                                    source: activityDelegate.modelData.icon && activityDelegate.modelData.icon !== "" ? activityDelegate.modelData.icon : "activities"
                                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: activityDelegate.activityName
                                    font.weight: activityDelegate.activityId === root.settingsBridge.currentActivity ? Font.DemiBold : Font.Normal
                                    elide: Text.ElideRight
                                }

                                Label {
                                    visible: activityDelegate.activityId === root.settingsBridge.currentActivity
                                    text: i18n("Current")
                                    font.italic: true
                                    opacity: 0.7
                                }

                            }

                            Repeater {
                                model: root.settingsBridge.screens

                                RowLayout {
                                    id: activityScreenRow

                                    required property var modelData
                                    required property int index
                                    property string screenName: modelData.name || ""
                                    property bool activityActive: !root.settingsBridge.isActivityDisabled(screenName, activityDelegate.activityId)

                                    Layout.fillWidth: true
                                    Layout.leftMargin: Kirigami.Units.gridUnit * 2
                                    spacing: Kirigami.Units.smallSpacing

                                    Kirigami.Icon {
                                        source: "video-display"
                                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                                        opacity: 0.7
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: activityScreenRow.screenName
                                        elide: Text.ElideRight
                                    }

                                    Switch {
                                        checked: activityScreenRow.activityActive
                                        Accessible.name: i18n("Scrolling mode for %1 on %2", activityDelegate.activityName, activityScreenRow.screenName)
                                        onToggled: {
                                            root.settingsBridge.setActivityDisabled(activityScreenRow.screenName, activityDelegate.activityId, !checked);
                                            activityScreenRow.activityActive = checked;
                                        }
                                        ToolTip.visible: hovered
                                        ToolTip.text: checked ? i18n("Disable scrolling mode for %1 on %2", activityDelegate.activityName, activityScreenRow.screenName) : i18n("Enable scrolling mode for %1 on %2", activityDelegate.activityName, activityScreenRow.screenName)

                                        Connections {
                                            function onDisabledActivitiesChanged() {
                                                activityScreenRow.activityActive = !root.settingsBridge.isActivityDisabled(activityScreenRow.screenName, activityDelegate.activityId);
                                            }

                                            target: root.settingsBridge
                                        }

                                    }

                                }

                            }

                        }

                    }

                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    Layout.margins: Kirigami.Units.smallSpacing
                    visible: root.settingsBridge.activities.length === 0
                    type: Kirigami.MessageType.Information
                    text: i18n("No activities found. Create activities in System Settings → Activities.")
                }

            }

        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: !root.settingsBridge.activitiesAvailable && root.settingsBridge.screens.length > 0
            type: Kirigami.MessageType.Information
            text: i18n("KDE Activities support is not available. Activity-based gating requires the KDE Activities service to be running.")
        }

    }

}
