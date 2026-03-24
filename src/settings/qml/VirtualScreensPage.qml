// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Settings page for virtual screen configuration.
 *
 * Allows splitting a physical monitor into multiple virtual screens
 * with preset layouts. Moved from the editor to the settings app
 * since this is a per-monitor configuration concern.
 */
Flickable {
    id: root

    // Internal state
    property var physicalScreenList: []
    property string selectedScreenId: ""
    property var currentVirtualScreens: []

    function refreshScreens() {
        physicalScreenList = settingsController.getPhysicalScreens();
        if (physicalScreenList.length > 0 && selectedScreenId === "")
            selectedScreenId = physicalScreenList[0];

        if (selectedScreenId !== "")
            refreshConfig();

    }

    function refreshConfig() {
        if (selectedScreenId === "")
            return ;

        currentVirtualScreens = settingsController.getVirtualScreenConfig(selectedScreenId);
    }

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        refreshScreens();
    }

    Connections {
        function onScreensChanged() {
            root.refreshScreens();
        }

        target: settingsController
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            text: i18n("Split a physical monitor into multiple virtual screens. Each virtual screen gets its own zone layout and acts as an independent display.")
            visible: true
        }

        // ═══════════════════════════════════════════════════════════════
        // SCREEN SELECTOR
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            headerText: i18n("Physical Screen")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                ComboBox {
                    id: screenCombo

                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    model: root.physicalScreenList
                    currentIndex: {
                        var idx = root.physicalScreenList.indexOf(root.selectedScreenId);
                        return idx >= 0 ? idx : 0;
                    }
                    displayText: root.selectedScreenId !== "" ? root.selectedScreenId : ""
                    onActivated: function(index) {
                        if (index >= 0 && index < root.physicalScreenList.length) {
                            root.selectedScreenId = root.physicalScreenList[index];
                            root.refreshConfig();
                        }
                    }

                    delegate: ItemDelegate {
                        required property string modelData
                        required property int index

                        width: screenCombo.width
                        text: modelData
                        highlighted: screenCombo.highlightedIndex === index
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // VISUAL PREVIEW
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            headerText: i18n("Preview")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                Rectangle {
                    id: previewRect

                    Layout.fillWidth: true
                    Layout.preferredHeight: width * 9 / 21 // Ultrawide aspect ratio
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
                    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                    border.width: 1
                    radius: Kirigami.Units.smallSpacing

                    // "No subdivisions" label when empty
                    Label {
                        anchors.centerIn: parent
                        visible: root.currentVirtualScreens.length === 0
                        text: i18n("No subdivisions (full screen)")
                        color: Kirigami.Theme.disabledTextColor
                        font.italic: true
                    }

                    // Virtual screen rectangles
                    Repeater {
                        model: root.currentVirtualScreens

                        Rectangle {
                            required property var modelData
                            required property int index

                            x: modelData.x * previewRect.width + 1
                            y: modelData.y * previewRect.height + 1
                            width: modelData.width * previewRect.width - 2
                            height: modelData.height * previewRect.height - 2
                            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)
                            border.color: Kirigami.Theme.highlightColor
                            border.width: 2
                            radius: Kirigami.Units.smallSpacing / 2

                            ColumnLayout {
                                anchors.centerIn: parent
                                spacing: 2

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: modelData.displayName || ""
                                    font.weight: Font.DemiBold
                                    font.pixelSize: Math.max(10, Math.min(14, parent.parent.width / 8))
                                    color: Kirigami.Theme.textColor
                                    elide: Text.ElideRight
                                    maximumLineCount: 1
                                }

                                Label {
                                    Layout.alignment: Qt.AlignHCenter
                                    text: Math.round(modelData.width * 100) + "%"
                                    font.pixelSize: Math.max(9, Math.min(12, parent.parent.width / 10))
                                    color: Kirigami.Theme.disabledTextColor
                                }

                            }

                        }

                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // PRESETS
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            headerText: i18n("Presets")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                GridLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    columns: 2
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Button {
                        Layout.fillWidth: true
                        text: i18n("50 / 50")
                        enabled: root.selectedScreenId !== ""
                        onClicked: {
                            settingsController.applyVirtualScreenPreset(root.selectedScreenId, "50-50");
                            root.refreshConfig();
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("60 / 40")
                        enabled: root.selectedScreenId !== ""
                        onClicked: {
                            settingsController.applyVirtualScreenPreset(root.selectedScreenId, "60-40");
                            root.refreshConfig();
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("33 / 33 / 33")
                        enabled: root.selectedScreenId !== ""
                        onClicked: {
                            settingsController.applyVirtualScreenPreset(root.selectedScreenId, "33-33-33");
                            root.refreshConfig();
                        }
                    }

                    Button {
                        Layout.fillWidth: true
                        text: i18n("40 / 20 / 40")
                        enabled: root.selectedScreenId !== ""
                        onClicked: {
                            settingsController.applyVirtualScreenPreset(root.selectedScreenId, "40-20-40");
                            root.refreshConfig();
                        }
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // VIRTUAL SCREEN LIST (editable names)
        // ═══════════════════════════════════════════════════════════════
        SettingsCard {
            visible: root.currentVirtualScreens.length > 0
            headerText: i18n("Virtual Screen Names")

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Repeater {
                    model: root.currentVirtualScreens

                    RowLayout {
                        required property var modelData
                        required property int index

                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            text: (index + 1) + "."
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 1.5
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                        }

                        TextField {
                            Layout.fillWidth: true
                            text: modelData.displayName || ""
                            placeholderText: i18n("Screen name")
                            onEditingFinished: {
                                var screens = root.currentVirtualScreens;
                                if (index >= 0 && index < screens.length) {
                                    var updated = screens[index];
                                    updated.displayName = text;
                                    screens[index] = updated;
                                    settingsController.applyVirtualScreenConfig(root.selectedScreenId, screens);
                                    root.refreshConfig();
                                }
                            }
                        }

                        Label {
                            text: Math.round(modelData.width * 100) + "%"
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                            horizontalAlignment: Text.AlignRight
                            color: Kirigami.Theme.disabledTextColor
                        }

                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════
        // REMOVE BUTTON
        // ═══════════════════════════════════════════════════════════════
        Button {
            Layout.fillWidth: true
            text: i18n("Remove Subdivisions")
            icon.name: "edit-delete"
            enabled: root.selectedScreenId !== "" && root.currentVirtualScreens.length > 0
            onClicked: {
                settingsController.removeVirtualScreenConfig(root.selectedScreenId);
                root.refreshConfig();
            }
        }

    }

}
