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

        // =====================================================================
        // APPEARANCE
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: appearanceCard.implicitHeight

            SettingsCard {
                id: appearanceCard

                anchors.fill: parent
                headerText: i18n("Appearance")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Colors")
                    }

                    CheckBox {
                        id: useSystemColorsCheck

                        Kirigami.FormData.label: i18n("Color scheme:")
                        text: i18n("Use system accent color")
                        checked: appSettings.useSystemColors
                        onToggled: appSettings.useSystemColors = checked
                    }

                    ColorSwatchRow {
                        formLabel: i18n("Highlight:")
                        visible: !useSystemColorsCheck.checked
                        color: appSettings.highlightColor
                        onClicked: {
                            highlightColorDialog.selectedColor = appSettings.highlightColor;
                            highlightColorDialog.open();
                        }
                    }

                    ColorSwatchRow {
                        formLabel: i18n("Inactive:")
                        visible: !useSystemColorsCheck.checked
                        color: appSettings.inactiveColor
                        onClicked: {
                            inactiveColorDialog.selectedColor = appSettings.inactiveColor;
                            inactiveColorDialog.open();
                        }
                    }

                    ColorSwatchRow {
                        formLabel: i18n("Border:")
                        visible: !useSystemColorsCheck.checked
                        color: appSettings.borderColor
                        onClicked: {
                            borderColorDialog.selectedColor = appSettings.borderColor;
                            borderColorDialog.open();
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Import colors:")
                        visible: !useSystemColorsCheck.checked
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

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    // Opacity subsection
                    SettingsSlider {
                        formLabel: i18n("Active opacity:")
                        from: 0
                        to: root.opacitySliderMax
                        value: appSettings.activeOpacity * root.opacitySliderMax
                        onMoved: (value) => {
                            return appSettings.activeOpacity = value / root.opacitySliderMax;
                        }
                    }

                    SettingsSlider {
                        formLabel: i18n("Inactive opacity:")
                        from: 0
                        to: root.opacitySliderMax
                        value: appSettings.inactiveOpacity * root.opacitySliderMax
                        onMoved: (value) => {
                            return appSettings.inactiveOpacity = value / root.opacitySliderMax;
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Border")
                    }

                    SettingsSpinBox {
                        formLabel: i18n("Border width:")
                        from: settingsController.borderWidthMin
                        to: root.borderWidthMax
                        value: appSettings.borderWidth
                        onValueModified: (value) => {
                            return appSettings.borderWidth = value;
                        }
                    }

                    SettingsSpinBox {
                        formLabel: i18n("Border radius:")
                        from: settingsController.borderRadiusMin
                        to: root.borderRadiusMax
                        value: appSettings.borderRadius
                        onValueModified: (value) => {
                            return appSettings.borderRadius = value;
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Zone Labels")
                    }

                    ColorSwatchRow {
                        formLabel: i18n("Color:")
                        visible: !useSystemColorsCheck.checked
                        color: appSettings.labelFontColor
                        onClicked: {
                            labelFontColorDialog.selectedColor = appSettings.labelFontColor;
                            labelFontColorDialog.open();
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Font:")
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
                            ToolTip.text: i18n("Reset to defaults")
                            ToolTip.visible: hovered
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

                    SettingsSlider {
                        formLabel: i18n("Scale:")
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

    // =========================================================================
    // COLOR DIALOGS
    // =========================================================================
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
