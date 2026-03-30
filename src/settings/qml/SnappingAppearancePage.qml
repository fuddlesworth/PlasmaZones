// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int opacitySliderMax: 100
    readonly property int borderWidthMax: settingsController.borderWidthMax
    readonly property int borderRadiusMax: settingsController.borderRadiusMax

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // COLORS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: colorsCard.implicitHeight

            SettingsCard {
                id: colorsCard

                anchors.fill: parent
                headerText: i18n("Colors")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("System accent color")
                        description: i18n("Use your desktop color scheme for zone colors")

                        SettingsSwitch {
                            id: useSystemColorsSwitch

                            checked: appSettings.useSystemColors
                            accessibleName: i18n("Use system accent color")
                            onToggled: appSettings.useSystemColors = checked
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Highlight color")
                        description: i18n("Color for the active/hovered zone")

                        ColorSwatchRow {
                            color: appSettings.highlightColor
                            onClicked: {
                                highlightColorDialog.selectedColor = appSettings.highlightColor;
                                highlightColorDialog.open();
                            }
                        }

                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Inactive color")
                        description: i18n("Color for zones that are not hovered")

                        ColorSwatchRow {
                            color: appSettings.inactiveColor
                            onClicked: {
                                inactiveColorDialog.selectedColor = appSettings.inactiveColor;
                                inactiveColorDialog.open();
                            }
                        }

                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Border color")
                        description: i18n("Color for zone borders")

                        ColorSwatchRow {
                            color: appSettings.borderColor
                            onClicked: {
                                borderColorDialog.selectedColor = appSettings.borderColor;
                                borderColorDialog.open();
                            }
                        }

                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Import colors")
                        description: i18n("Load a color scheme from pywal or a JSON file")

                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: i18n("From pywal")
                                icon.name: "color-management"
                                onClicked: settingsController.loadColorsFromPywal()
                            }

                            Button {
                                text: i18n("From file...")
                                icon.name: "document-open"
                                onClicked: colorFileDialog.open()
                            }

                        }

                    }

                    Kirigami.InlineMessage {
                        id: colorImportMessage

                        Layout.fillWidth: true
                        visible: false
                        type: Kirigami.MessageType.Positive
                        Accessible.name: text
                    }

                    Timer {
                        id: colorImportHideTimer

                        interval: 3000
                        onTriggered: colorImportMessage.visible = false
                    }

                }

            }

        }

        // =================================================================
        // OPACITY
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: opacityCard.implicitHeight

            SettingsCard {
                id: opacityCard

                anchors.fill: parent
                headerText: i18n("Opacity")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Active opacity")
                        description: i18n("Opacity of the zone under the cursor")

                        SettingsSlider {
                            from: 0
                            to: root.opacitySliderMax
                            value: appSettings.activeOpacity * root.opacitySliderMax
                            onMoved: (value) => {
                                return appSettings.activeOpacity = value / root.opacitySliderMax;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Inactive opacity")
                        description: i18n("Opacity of zones not under the cursor")

                        SettingsSlider {
                            from: 0
                            to: root.opacitySliderMax
                            value: appSettings.inactiveOpacity * root.opacitySliderMax
                            onMoved: (value) => {
                                return appSettings.inactiveOpacity = value / root.opacitySliderMax;
                            }
                        }

                    }

                }

            }

        }

        // =================================================================
        // BORDER
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: borderCard.implicitHeight

            SettingsCard {
                id: borderCard

                anchors.fill: parent
                headerText: i18n("Border")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Border width")
                        description: i18n("Thickness of zone borders in pixels")

                        SettingsSpinBox {
                            from: settingsController.borderWidthMin
                            to: root.borderWidthMax
                            value: appSettings.borderWidth
                            onValueModified: (value) => {
                                return appSettings.borderWidth = value;
                            }
                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Border radius")
                        description: i18n("Corner rounding of zone borders in pixels")

                        SettingsSpinBox {
                            from: settingsController.borderRadiusMin
                            to: root.borderRadiusMax
                            value: appSettings.borderRadius
                            onValueModified: (value) => {
                                return appSettings.borderRadius = value;
                            }
                        }

                    }

                }

            }

        }

        // =================================================================
        // ZONE LABELS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: labelsCard.implicitHeight

            SettingsCard {
                id: labelsCard

                anchors.fill: parent
                headerText: i18n("Zone Labels")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        visible: !useSystemColorsSwitch.checked
                        title: i18n("Label color")
                        description: i18n("Text color for zone labels")

                        ColorSwatchRow {
                            color: appSettings.labelFontColor
                            onClicked: {
                                labelFontColorDialog.selectedColor = appSettings.labelFontColor;
                                labelFontColorDialog.open();
                            }
                        }

                    }

                    SettingsSeparator {
                        visible: !useSystemColorsSwitch.checked
                    }

                    SettingsRow {
                        title: i18n("Font")
                        description: i18n("Typeface and style for zone labels")

                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing

                            Button {
                                text: appSettings.labelFontFamily || i18n("System default")
                                font.family: appSettings.labelFontFamily
                                font.weight: appSettings.labelFontWeight
                                font.italic: appSettings.labelFontItalic
                                icon.name: "font-select-symbolic"
                                onClicked: {
                                    fontPickerDialog.selectedFamily = appSettings.labelFontFamily;
                                    fontPickerDialog.selectedWeight = appSettings.labelFontWeight;
                                    fontPickerDialog.selectedItalic = appSettings.labelFontItalic;
                                    fontPickerDialog.selectedUnderline = appSettings.labelFontUnderline;
                                    fontPickerDialog.selectedStrikeout = appSettings.labelFontStrikeout;
                                    fontPickerDialog.open();
                                }
                            }

                            Button {
                                icon.name: "edit-clear"
                                visible: appSettings.labelFontFamily !== "" || appSettings.labelFontWeight !== Font.Bold || appSettings.labelFontItalic || appSettings.labelFontUnderline || appSettings.labelFontStrikeout || Math.abs(appSettings.labelFontSizeScale - 1) > 0.01
                                Accessible.name: i18n("Reset to defaults")
                                onClicked: {
                                    appSettings.labelFontFamily = "";
                                    appSettings.labelFontSizeScale = 1;
                                    appSettings.labelFontWeight = Font.Bold;
                                    appSettings.labelFontItalic = false;
                                    appSettings.labelFontUnderline = false;
                                    appSettings.labelFontStrikeout = false;
                                }
                            }

                        }

                    }

                    SettingsSeparator {
                    }

                    SettingsRow {
                        title: i18n("Label scale")
                        description: i18n("Size multiplier for zone label text")

                        SettingsSlider {
                            from: 25
                            to: 300
                            stepSize: 5
                            value: appSettings.labelFontSizeScale * 100
                            onMoved: (value) => {
                                return appSettings.labelFontSizeScale = value / 100;
                            }
                        }

                    }

                }

            }

        }

    }

    // =====================================================================
    // COLOR DIALOGS
    // =====================================================================
    ColorDialog {
        id: highlightColorDialog

        title: i18n("Choose Highlight Color")
        onAccepted: appSettings.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog

        title: i18n("Choose Inactive Zone Color")
        onAccepted: appSettings.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog

        title: i18n("Choose Border Color")
        onAccepted: appSettings.borderColor = selectedColor
    }

    ColorDialog {
        id: labelFontColorDialog

        title: i18n("Choose Label Color")
        onAccepted: appSettings.labelFontColor = selectedColor
    }

    FontPickerDialog {
        id: fontPickerDialog

        appSettings: settingsController
        onAccepted: {
            appSettings.labelFontFamily = selectedFamily;
            appSettings.labelFontWeight = selectedWeight;
            appSettings.labelFontItalic = selectedItalic;
            appSettings.labelFontUnderline = selectedUnderline;
            appSettings.labelFontStrikeout = selectedStrikeout;
        }
    }

    FileDialog {
        id: colorFileDialog

        title: i18n("Import Colors from File")
        nameFilters: [i18n("JSON files (*.json)"), i18n("All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: settingsController.loadColorsFromFile(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    Kirigami.PromptDialog {
        id: colorImportErrorDialog

        title: i18n("Color Import Failed")
        subtitle: ""
        standardButtons: Kirigami.Dialog.Ok
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, parent.width * 0.8)
    }

    Connections {
        function onColorImportError(message) {
            colorImportErrorDialog.subtitle = message;
            colorImportErrorDialog.open();
        }

        function onColorImportSuccess() {
            colorImportMessage.type = Kirigami.MessageType.Positive;
            colorImportMessage.text = i18n("Colors imported successfully");
            colorImportMessage.visible = true;
            colorImportHideTimer.restart();
        }

        target: settingsController
    }

}
