// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Visual monitor selector with KCM display settings-style icons.
 *
 * Shows a horizontal row of clickable monitor representations.
 * "All Monitors" is the default; clicking a specific monitor selects it
 * for per-screen overrides. Active monitor has an accent border.
 */
ColumnLayout {
    id: root

    required property var appSettings
    property string selectedScreenName: ""
    property bool hasOverrides: false
    property bool showAllMonitors: true
    property bool physicalOnly: false // When true, filter out virtual screens (e.g., for virtual screen config page)
    readonly property bool isPerScreen: selectedScreenName !== ""
    // Filtered screen list: removes virtual screens when physicalOnly is true
    readonly property var _filteredScreens: {
        var all = appSettings.screens;
        if (!physicalOnly)
            return all;

        var result = [];
        for (var i = 0; i < all.length; i++) {
            var name = all[i].name || "";
            if (name.indexOf("/vs:") === -1)
                result.push(all[i]);

        }
        return result;
    }

    signal resetClicked()

    visible: showAllMonitors ? _filteredScreens.length > 1 : _filteredScreens.length > 0
    spacing: Kirigami.Units.smallSpacing

    // Hot-unplug: reset selection if selected screen disappears
    Connections {
        function onScreensChanged() {
            if (root.selectedScreenName === "")
                return ;

            let screens = root._filteredScreens;
            for (let i = 0; i < screens.length; i++) {
                if (screens[i].name === root.selectedScreenName)
                    return ;

            }
            root.selectedScreenName = "";
        }

        target: root.appSettings
    }

    // Monitor icons row — centered via Item wrapper
    Item {
        Layout.fillWidth: true
        implicitHeight: monitorRow.implicitHeight

        RowLayout {
            id: monitorRow

            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Kirigami.Units.largeSpacing

            // "All Monitors" option
            Rectangle {
                visible: root.showAllMonitors
                width: allMonitorContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                height: allMonitorContent.implicitHeight + Kirigami.Units.largeSpacing
                radius: Kirigami.Units.smallSpacing
                color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : allMonitorMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                border.width: Math.round(Kirigami.Units.devicePixelRatio)
                border.color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                Accessible.role: Accessible.RadioButton
                Accessible.name: i18n("All Monitors")
                Accessible.checked: !root.isPerScreen

                ColumnLayout {
                    id: allMonitorContent

                    anchors.centerIn: parent
                    spacing: Kirigami.Units.smallSpacing / 2

                    Kirigami.Icon {
                        source: "monitor"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                        Layout.alignment: Qt.AlignHCenter
                        opacity: !root.isPerScreen ? 1 : 0.5
                    }

                    Label {
                        text: i18n("All Monitors")
                        font: Kirigami.Theme.smallFont
                        Layout.alignment: Qt.AlignHCenter
                        opacity: !root.isPerScreen ? 1 : 0.5
                    }

                }

                MouseArea {
                    id: allMonitorMouse

                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    hoverEnabled: true
                    onClicked: root.selectedScreenName = ""
                }

                Behavior on color {
                    ColorAnimation {
                        duration: 150
                    }

                }

                Behavior on border.color {
                    ColorAnimation {
                        duration: 150
                    }

                }

            }

            // Individual monitors
            Repeater {
                model: root._filteredScreens

                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    readonly property string screenName: modelData.name || ""
                    readonly property bool isSelected: root.selectedScreenName === screenName

                    width: monitorContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                    height: monitorContent.implicitHeight + Kirigami.Units.largeSpacing
                    radius: Kirigami.Units.smallSpacing
                    color: isSelected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.1) : monitorMouse.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                    border.width: Math.round(Kirigami.Units.devicePixelRatio)
                    border.color: isSelected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: screenName
                    Accessible.checked: isSelected

                    ColumnLayout {
                        id: monitorContent

                        anchors.centerIn: parent
                        spacing: Kirigami.Units.smallSpacing / 2

                        Kirigami.Icon {
                            source: "monitor"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                            Layout.alignment: Qt.AlignHCenter
                            opacity: isSelected ? 1 : 0.5
                        }

                        Label {
                            text: {
                                let s = modelData;
                                let parts = [];
                                if (s.manufacturer)
                                    parts.push(s.manufacturer);

                                if (s.model)
                                    parts.push(s.model);

                                if (parts.length === 0)
                                    parts.push(screenName);

                                let label = parts.join(" ");
                                if (s.isPrimary)
                                    label += " " + i18n("(Primary)");

                                return label;
                            }
                            font: Kirigami.Theme.smallFont
                            Layout.alignment: Qt.AlignHCenter
                            opacity: isSelected ? 1 : 0.5
                            elide: Text.ElideRight
                            Layout.maximumWidth: Kirigami.Units.gridUnit * 15
                        }

                    }

                    MouseArea {
                        id: monitorMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        onClicked: root.selectedScreenName = screenName
                    }

                    Behavior on color {
                        ColorAnimation {
                            duration: 150
                        }

                    }

                    Behavior on border.color {
                        ColorAnimation {
                            duration: 150
                        }

                    }

                }

            }

        }

    }

    // Per-screen info/reset row
    RowLayout {
        Layout.fillWidth: true
        visible: root.isPerScreen && root.showAllMonitors
        spacing: Kirigami.Units.smallSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: root.hasOverrides ? Kirigami.MessageType.Positive : Kirigami.MessageType.Information
            text: root.hasOverrides ? i18n("Custom settings for this monitor") : i18n("Using default settings (editing below will create an override)")
            visible: true
        }

        Button {
            text: i18n("Reset")
            icon.name: "edit-clear"
            flat: true
            visible: root.hasOverrides
            onClicked: root.resetClicked()
        }

    }

}
