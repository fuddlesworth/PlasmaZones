// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dialog for configuring virtual screen subdivisions
 *
 * Allows splitting a physical monitor into multiple virtual screens
 * with preset layouts or custom configurations.
 */
Kirigami.Dialog {
    id: root

    required property var virtualScreenService
    // Internal state
    property var physicalScreenList: []
    property string selectedScreenId: ""
    property var currentVirtualScreens: []
    property var currentScreenInfo: ({
    })

    function refreshScreens() {
        if (!root.virtualScreenService)
            return ;

        physicalScreenList = root.virtualScreenService.physicalScreens();
        if (physicalScreenList.length > 0 && selectedScreenId === "")
            selectedScreenId = physicalScreenList[0];

        if (selectedScreenId !== "")
            refreshConfig();

    }

    function refreshConfig() {
        if (!root.virtualScreenService || selectedScreenId === "")
            return ;

        currentScreenInfo = root.virtualScreenService.screenInfo(selectedScreenId);
        currentVirtualScreens = root.virtualScreenService.virtualScreensFor(selectedScreenId);
    }

    function screenDisplayLabel(screenId) {
        if (!root.virtualScreenService)
            return screenId;

        var info = root.virtualScreenService.screenInfo(screenId);
        if (info && info.manufacturer && info.model)
            return info.manufacturer + " " + info.model + " (" + screenId + ")";

        return screenId;
    }

    title: i18nc("@title:window", "Virtual Screens")
    standardButtons: Kirigami.Dialog.NoButton
    preferredWidth: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing
    onOpened: {
        refreshScreens();
    }

    Connections {
        function onConfigChanged(physicalScreenId) {
            if (physicalScreenId === root.selectedScreenId)
                refreshConfig();

        }

        target: root.virtualScreenService
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════
        // SCREEN SELECTOR
        // ═══════════════════════════════════════════════════════════════
        SectionHeader {
            title: i18nc("@title:group", "Physical Screen")
            icon: "monitor"
        }

        ComboBox {
            id: screenCombo

            Layout.fillWidth: true
            model: root.physicalScreenList
            currentIndex: {
                var idx = root.physicalScreenList.indexOf(root.selectedScreenId);
                return idx >= 0 ? idx : 0;
            }
            displayText: root.selectedScreenId !== "" ? root.screenDisplayLabel(root.selectedScreenId) : ""
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
                text: root.screenDisplayLabel(modelData)
                highlighted: screenCombo.highlightedIndex === index
            }

        }

        // Screen resolution info
        Label {
            visible: root.currentScreenInfo && root.currentScreenInfo.geometry !== undefined
            text: {
                if (!root.currentScreenInfo || !root.currentScreenInfo.geometry)
                    return "";

                var geom = root.currentScreenInfo.geometry;
                return i18nc("@info screen resolution", "%1 x %2", geom.width || 0, geom.height || 0);
            }
            font.italic: true
            color: Kirigami.Theme.disabledTextColor
        }

        // ═══════════════════════════════════════════════════════════════
        // VISUAL PREVIEW
        // ═══════════════════════════════════════════════════════════════
        SectionHeader {
            title: i18nc("@title:group", "Preview")
            icon: "view-preview"
        }

        Rectangle {
            id: previewRect

            Layout.fillWidth: true
            Layout.preferredHeight: width * 9 / 16
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.5)
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
            border.width: 1
            radius: Kirigami.Units.smallSpacing

            // "No subdivisions" label when empty
            Label {
                anchors.centerIn: parent
                visible: root.currentVirtualScreens.length === 0
                text: i18nc("@info", "No subdivisions (full screen)")
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

        // ═══════════════════════════════════════════════════════════════
        // PRESETS
        // ═══════════════════════════════════════════════════════════════
        SectionHeader {
            title: i18nc("@title:group", "Presets")
            icon: "view-grid"
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Kirigami.Units.smallSpacing
            rowSpacing: Kirigami.Units.smallSpacing

            Button {
                Layout.fillWidth: true
                text: i18nc("@action:button virtual screen preset", "50 / 50")
                enabled: root.selectedScreenId !== ""
                onClicked: {
                    root.virtualScreenService.applyPreset(root.selectedScreenId, "50-50");
                    root.refreshConfig();
                }
            }

            Button {
                Layout.fillWidth: true
                text: i18nc("@action:button virtual screen preset", "60 / 40")
                enabled: root.selectedScreenId !== ""
                onClicked: {
                    root.virtualScreenService.applyPreset(root.selectedScreenId, "60-40");
                    root.refreshConfig();
                }
            }

            Button {
                Layout.fillWidth: true
                text: i18nc("@action:button virtual screen preset", "33 / 33 / 33")
                enabled: root.selectedScreenId !== ""
                onClicked: {
                    root.virtualScreenService.applyPreset(root.selectedScreenId, "33-33-33");
                    root.refreshConfig();
                }
            }

            Button {
                Layout.fillWidth: true
                text: i18nc("@action:button virtual screen preset", "40 / 20 / 40")
                enabled: root.selectedScreenId !== ""
                onClicked: {
                    root.virtualScreenService.applyPreset(root.selectedScreenId, "40-20-40");
                    root.refreshConfig();
                }
            }

        }

        // ═══════════════════════════════════════════════════════════════
        // VIRTUAL SCREEN LIST (editable names)
        // ═══════════════════════════════════════════════════════════════
        Repeater {
            model: root.currentVirtualScreens

            RowLayout {
                required property var modelData
                required property int index

                Layout.fillWidth: true
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
                    placeholderText: i18nc("@info:placeholder", "Screen name")
                    onEditingFinished: {
                        // Update the name in the current config and re-apply
                        var screens = root.currentVirtualScreens;
                        if (index >= 0 && index < screens.length) {
                            var updated = screens[index];
                            updated.displayName = text;
                            screens[index] = updated;
                            root.virtualScreenService.applyConfig(root.selectedScreenId, screens);
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

        // ═══════════════════════════════════════════════════════════════
        // REMOVE BUTTON
        // ═══════════════════════════════════════════════════════════════
        Button {
            Layout.fillWidth: true
            text: i18nc("@action:button", "Remove Subdivisions")
            icon.name: "edit-delete"
            enabled: root.selectedScreenId !== "" && root.currentVirtualScreens.length > 0
            onClicked: {
                root.virtualScreenService.removeConfig(root.selectedScreenId);
                root.refreshConfig();
            }
        }

    }

}
