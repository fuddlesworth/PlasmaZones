// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// Snapping → Overlay → Appearance. How the drag-time zone overlay LOOKS: zone
// colours, opacity, borders and labels (the former "Zones" page) merged with
// the overlay effects (blur, numbers, flash, shaders, audio — the former
// "Effects" page). Binds to two controllers: snappingZonesPage (colour import +
// border/label bounds) and snappingEffectsPage (shader/audio bounds + CAVA).
SettingsFlickable {
    id: root

    readonly property var zonesBridge: settingsController.snappingZonesPage
    readonly property var effectsBridge: settingsController.snappingEffectsPage
    readonly property int opacitySliderMax: 100

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
                            onToggled: function (newValue) {
                                appSettings.useSystemColors = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

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
                                onClicked: root.zonesBridge.loadColorsFromPywal()
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
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
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
                            onMoved: value => {
                                return appSettings.activeOpacity = value / root.opacitySliderMax;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Inactive opacity")
                        description: i18n("Opacity of zones not under the cursor")

                        SettingsSlider {
                            from: 0
                            to: root.opacitySliderMax
                            value: appSettings.inactiveOpacity * root.opacitySliderMax
                            onMoved: value => {
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
                            from: root.zonesBridge.borderWidthMin
                            to: root.zonesBridge.borderWidthMax
                            value: appSettings.borderWidth
                            onValueModified: value => {
                                return appSettings.borderWidth = value;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Border radius")
                        description: i18n("Corner rounding of zone borders in pixels")

                        SettingsSpinBox {
                            from: root.zonesBridge.borderRadiusMin
                            to: root.zonesBridge.borderRadiusMax
                            value: appSettings.borderRadius
                            onValueModified: value => {
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

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Label scale")
                        description: i18n("Size multiplier for zone label text")

                        SettingsSlider {
                            from: root.zonesBridge.labelFontScaleMin * 100
                            to: root.zonesBridge.labelFontScaleMax * 100
                            stepSize: 5
                            value: appSettings.labelFontSizeScale * 100
                            onMoved: value => {
                                return appSettings.labelFontSizeScale = value / 100;
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // EFFECTS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: effectsCard.implicitHeight

            SettingsCard {
                id: effectsCard

                anchors.fill: parent
                headerText: i18n("Effects")
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Blur behind zones")
                        description: i18n("Apply a blur effect to the area behind zone overlays")

                        SettingsSwitch {
                            checked: appSettings.enableBlur
                            accessibleName: i18n("Enable blur behind zones")
                            onToggled: function (newValue) {
                                appSettings.enableBlur = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Zone numbers")
                        description: i18n("Display a number label inside each zone")

                        SettingsSwitch {
                            checked: appSettings.showZoneNumbers
                            accessibleName: i18n("Show zone numbers")
                            onToggled: function (newValue) {
                                appSettings.showZoneNumbers = newValue;
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Flash on layout switch")
                        description: i18n("Briefly flash zones when switching between layouts")

                        SettingsSwitch {
                            checked: appSettings.flashZonesOnSwitch
                            accessibleName: i18n("Flash zones on layout switch")
                            onToggled: function (newValue) {
                                appSettings.flashZonesOnSwitch = newValue;
                            }
                        }
                    }
                }
            }
        }

        // =================================================================
        // SHADER EFFECTS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: shaderCard.implicitHeight

            SettingsCard {
                id: shaderCard

                anchors.fill: parent
                headerText: i18n("Shader Effects")
                showToggle: true
                toggleChecked: appSettings.enableShaderEffects
                collapsible: true
                onToggleClicked: checked => {
                    return appSettings.enableShaderEffects = checked;
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Frame rate")
                        description: i18n("Target refresh rate for shader animations")

                        SettingsSlider {
                            from: root.effectsBridge.shaderFrameRateMin
                            to: root.effectsBridge.shaderFrameRateMax
                            value: appSettings.shaderFrameRate
                            valueSuffix: " fps"
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.shaderFrameRate = Math.round(value);
                            }
                        }
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Audio spectrum")
                        description: root.effectsBridge.cavaAvailable ? i18n("Feed audio spectrum data to shaders that support it") : i18n("CAVA is not installed. Install cava to enable audio visualization.")

                        SettingsSwitch {
                            id: audioVizSwitch

                            enabled: root.effectsBridge.cavaAvailable
                            checked: appSettings.enableAudioVisualizer
                            accessibleName: i18n("Enable CAVA audio spectrum")
                            onToggled: function (newValue) {
                                appSettings.enableAudioVisualizer = newValue;
                            }
                        }
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        type: Kirigami.MessageType.Warning
                        text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                        visible: !root.effectsBridge.cavaAvailable && shaderCard.toggleChecked
                    }

                    SettingsSeparator {}

                    SettingsRow {
                        title: i18n("Spectrum bars")
                        description: i18n("Number of frequency bands in the audio visualization")
                        enabled: audioVizSwitch.checked && root.effectsBridge.cavaAvailable

                        SettingsSlider {
                            from: root.effectsBridge.audioSpectrumBarCountMin
                            to: root.effectsBridge.audioSpectrumBarCountMax
                            stepSize: 2
                            value: appSettings.audioSpectrumBarCount
                            valueSuffix: ""
                            labelWidth: Kirigami.Units.gridUnit * 4
                            onMoved: value => {
                                return appSettings.audioSpectrumBarCount = Math.round(value);
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

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Highlight Color")
        onAccepted: appSettings.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Zone Color")
        onAccepted: appSettings.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Border Color")
        onAccepted: appSettings.borderColor = selectedColor
    }

    ColorDialog {
        id: labelFontColorDialog

        options: ColorDialog.ShowAlphaChannel
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
        onAccepted: root.zonesBridge.loadColorsFromFile(settingsController.urlToLocalFile(selectedFile))
    }

    Kirigami.PromptDialog {
        id: colorImportErrorDialog

        title: i18n("Color Import Failed")
        standardButtons: Kirigami.Dialog.Ok
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, parent.width * 0.8)
    }

    Connections {
        function onColorImportError(message) {
            colorImportErrorDialog.subtitle = message;
            colorImportErrorDialog.open();
        }

        function onColorImportSuccess() {
            colorImportMessage.text = i18n("Colors imported successfully");
            colorImportMessage.visible = true;
            colorImportHideTimer.restart();
        }

        target: root.zonesBridge
    }
}
