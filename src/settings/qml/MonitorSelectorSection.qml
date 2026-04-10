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
    // Filtered screen list: when physicalOnly, deduplicate to physical screens only
    // (virtual screens like "id/vs:0" are collapsed to their physical parent "id").
    // Uses imperative JS because QML declarative bindings can't express set-deduplication.
    readonly property var _filteredScreens: {
        var all = appSettings.screens;
        if (!physicalOnly)
            return all;

        // Collect unique physical screen IDs
        var seen = {
        };
        var result = [];
        for (var i = 0; i < all.length; i++) {
            var name = all[i].name || "";
            var vsIdx = name.indexOf("/vs:");
            var physId = vsIdx >= 0 ? name.substring(0, vsIdx) : name;
            if (!seen[physId]) {
                seen[physId] = true;
                // Use the physical screen's data (first occurrence or non-virtual entry)
                var entry = {
                };
                for (var key in all[i]) entry[key] = all[i][key]
                entry["name"] = physId; // Override name to physical ID
                // Virtual screen entries have sub-geometry and VS-specific labels;
                // replace with the physical monitor's info.
                if (all[i].isVirtualScreen) {
                    delete entry["isVirtualScreen"];
                    delete entry["virtualIndex"];
                    delete entry["virtualDisplayName"];
                    // Find the physical screen's full resolution from Qt
                    var physRes = appSettings.physicalScreenResolution(physId);
                    if (physRes.width > 0 && physRes.height > 0) {
                        entry["width"] = physRes.width;
                        entry["height"] = physRes.height;
                        entry["resolution"] = physRes.width + "\u00d7" + physRes.height;
                    } else {
                        delete entry["resolution"];
                        delete entry["width"];
                        delete entry["height"];
                    }
                    // Rebuild displayLabel for the physical monitor
                    var parts = [];
                    if (entry.manufacturer)
                        parts.push(entry.manufacturer);

                    if (entry.model)
                        parts.push(entry.model);

                    var label = parts.length > 0 ? parts.join(" ") : physId;
                    if (entry.resolution)
                        label += " (" + entry.resolution + ")";

                    entry["displayLabel"] = label;
                }
                result.push(entry);
            }
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
                border.color: !root.isPerScreen ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : allMonitorMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
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
                    activeFocusOnTab: true
                    Keys.onSpacePressed: clicked(null)
                    Keys.onReturnPressed: clicked(null)
                    onClicked: root.selectedScreenName = ""
                }

                Behavior on color {
                    ColorAnimation {
                        duration: Kirigami.Units.shortDuration
                    }

                }

                Behavior on border.color {
                    ColorAnimation {
                        duration: Kirigami.Units.shortDuration
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
                    border.color: isSelected ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5) : monitorMouse.activeFocus ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                    Accessible.role: Accessible.RadioButton
                    Accessible.name: monitorLabel.text
                    Accessible.checked: isSelected

                    ColumnLayout {
                        id: monitorContent

                        readonly property bool isPortrait: (modelData.width || 0) > 0 && (modelData.height || 0) > 0 && modelData.height > modelData.width

                        anchors.centerIn: parent
                        spacing: Kirigami.Units.smallSpacing / 2

                        Kirigami.Icon {
                            source: "monitor"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                            Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                            Layout.alignment: Qt.AlignHCenter
                            opacity: isSelected ? 1 : 0.5
                            rotation: monitorContent.isPortrait ? 90 : 0
                        }

                        Label {
                            id: monitorLabel

                            text: {
                                let s = modelData;
                                // Use pre-computed displayLabel, append connector name for detail
                                let label = s.displayLabel || s.name || screenName;
                                if (s.connectorName)
                                    label += " · " + s.connectorName;

                                return label;
                            }
                            font: Kirigami.Theme.smallFont
                            Layout.alignment: Qt.AlignHCenter
                            opacity: isSelected ? 1 : 0.5
                            elide: Text.ElideRight
                            Layout.maximumWidth: Kirigami.Units.gridUnit * 15
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignHCenter
                            width: primaryLabel.implicitWidth + Kirigami.Units.smallSpacing * 2
                            height: primaryLabel.implicitHeight + 2
                            radius: height / 2
                            color: (modelData.isPrimary || false) ? Qt.rgba(Kirigami.Theme.positiveTextColor.r, Kirigami.Theme.positiveTextColor.g, Kirigami.Theme.positiveTextColor.b, 0.15) : "transparent"

                            Label {
                                id: primaryLabel

                                anchors.centerIn: parent
                                text: i18n("Primary")
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize - 1
                                color: Kirigami.Theme.positiveTextColor
                                opacity: (modelData.isPrimary || false) ? 1 : 0
                            }

                        }

                    }

                    MouseArea {
                        id: monitorMouse

                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        hoverEnabled: true
                        activeFocusOnTab: true
                        Keys.onSpacePressed: clicked(null)
                        Keys.onReturnPressed: clicked(null)
                        onClicked: root.selectedScreenName = screenName
                    }

                    Behavior on color {
                        ColorAnimation {
                            duration: Kirigami.Units.shortDuration
                        }

                    }

                    Behavior on border.color {
                        ColorAnimation {
                            duration: Kirigami.Units.shortDuration
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
