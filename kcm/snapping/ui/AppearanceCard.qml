// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Appearance + Effects cards for the Snapping sub-KCM.
 *
 * Contains color settings, opacity sliders, border, zone labels, blur,
 * shader effects, and audio visualization controls.
 *
 * Required properties:
 *   - kcm: the KCM backend object
 *   - constants: root object providing sliderPreferredWidth, sliderValueLabelWidth,
 *                opacitySliderMax, borderWidthMax, borderRadiusMax
 */
ColumnLayout {
    // ─────────────────────────────────────────────────────────────────────
    // DIALOGS (owned by this component since they reference appearance properties)
    // ─────────────────────────────────────────────────────────────────────

    id: cardRoot

    required property var kcm
    required property var constants

    spacing: Kirigami.Units.largeSpacing

    // ═══════════════════════════════════════════════════════════════════════
    // APPEARANCE CARD
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        Layout.fillWidth: true
        implicitHeight: appearanceCard.implicitHeight

        Kirigami.Card {
            id: appearanceCard

            anchors.fill: parent
            enabled: cardRoot.kcm.snappingEnabled

            header: Kirigami.Heading {
                level: 3
                text: i18n("Appearance")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Colors")
                }

                CheckBox {
                    id: useSystemColorsCheck

                    Kirigami.FormData.label: i18n("Color scheme:")
                    text: i18n("Use system accent color")
                    checked: cardRoot.kcm.useSystemColors
                    onToggled: cardRoot.kcm.useSystemColors = checked
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Highlight:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    ColorButton {
                        color: cardRoot.kcm.highlightColor
                        onClicked: {
                            highlightColorDialog.selectedColor = cardRoot.kcm.highlightColor;
                            highlightColorDialog.open();
                        }
                    }

                    Label {
                        text: cardRoot.kcm.highlightColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Inactive:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    ColorButton {
                        color: cardRoot.kcm.inactiveColor
                        onClicked: {
                            inactiveColorDialog.selectedColor = cardRoot.kcm.inactiveColor;
                            inactiveColorDialog.open();
                        }
                    }

                    Label {
                        text: cardRoot.kcm.inactiveColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Border:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    ColorButton {
                        color: cardRoot.kcm.borderColor
                        onClicked: {
                            borderColorDialog.selectedColor = cardRoot.kcm.borderColor;
                            borderColorDialog.open();
                        }
                    }

                    Label {
                        text: cardRoot.kcm.borderColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Import colors:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Button {
                        text: i18n("From pywal")
                        icon.name: "color-management"
                        onClicked: cardRoot.kcm.loadColorsFromPywal()
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
                RowLayout {
                    Kirigami.FormData.label: i18n("Active opacity:")
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: activeOpacitySlider

                        Layout.preferredWidth: cardRoot.constants.sliderPreferredWidth
                        from: 0
                        to: cardRoot.constants.opacitySliderMax
                        value: cardRoot.kcm.activeOpacity * cardRoot.constants.opacitySliderMax
                        onMoved: cardRoot.kcm.activeOpacity = value / cardRoot.constants.opacitySliderMax
                        Accessible.name: i18n("Active zone opacity")
                    }

                    Label {
                        text: Math.round(activeOpacitySlider.value) + "%"
                        Layout.preferredWidth: cardRoot.constants.sliderValueLabelWidth
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Inactive opacity:")
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: inactiveOpacitySlider

                        Layout.preferredWidth: cardRoot.constants.sliderPreferredWidth
                        from: 0
                        to: cardRoot.constants.opacitySliderMax
                        value: cardRoot.kcm.inactiveOpacity * cardRoot.constants.opacitySliderMax
                        onMoved: cardRoot.kcm.inactiveOpacity = value / cardRoot.constants.opacitySliderMax
                        Accessible.name: i18n("Inactive zone opacity")
                    }

                    Label {
                        text: Math.round(inactiveOpacitySlider.value) + "%"
                        Layout.preferredWidth: cardRoot.constants.sliderValueLabelWidth
                    }

                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Border")
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Border width:")
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: cardRoot.constants.borderWidthMax
                        value: cardRoot.kcm.borderWidth
                        onValueModified: cardRoot.kcm.borderWidth = value
                        Accessible.name: i18n("Border width")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Border radius:")
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: cardRoot.constants.borderRadiusMax
                        value: cardRoot.kcm.borderRadius
                        onValueModified: cardRoot.kcm.borderRadius = value
                        Accessible.name: i18n("Border radius")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Zone Labels")
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Color:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    ColorButton {
                        color: cardRoot.kcm.labelFontColor
                        onClicked: {
                            labelFontColorDialog.selectedColor = cardRoot.kcm.labelFontColor;
                            labelFontColorDialog.open();
                        }
                    }

                    Label {
                        text: cardRoot.kcm.labelFontColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Font:")
                    spacing: Kirigami.Units.smallSpacing

                    Button {
                        text: cardRoot.kcm.labelFontFamily || i18n("System default")
                        font.family: cardRoot.kcm.labelFontFamily
                        font.weight: cardRoot.kcm.labelFontWeight
                        font.italic: cardRoot.kcm.labelFontItalic
                        icon.name: "font-select-symbolic"
                        onClicked: {
                            fontDialog.selectedFamily = cardRoot.kcm.labelFontFamily;
                            fontDialog.selectedWeight = cardRoot.kcm.labelFontWeight;
                            fontDialog.selectedItalic = cardRoot.kcm.labelFontItalic;
                            fontDialog.selectedUnderline = cardRoot.kcm.labelFontUnderline;
                            fontDialog.selectedStrikeout = cardRoot.kcm.labelFontStrikeout;
                            fontDialog.open();
                        }
                    }

                    Button {
                        icon.name: "edit-clear"
                        visible: cardRoot.kcm.labelFontFamily !== "" || cardRoot.kcm.labelFontWeight !== Font.Bold || cardRoot.kcm.labelFontItalic || cardRoot.kcm.labelFontUnderline || cardRoot.kcm.labelFontStrikeout || Math.abs(cardRoot.kcm.labelFontSizeScale - 1) > 0.01
                        ToolTip.text: i18n("Reset to defaults")
                        ToolTip.visible: hovered
                        onClicked: {
                            cardRoot.kcm.labelFontFamily = "";
                            cardRoot.kcm.labelFontSizeScale = 1;
                            cardRoot.kcm.labelFontWeight = Font.Bold;
                            cardRoot.kcm.labelFontItalic = false;
                            cardRoot.kcm.labelFontUnderline = false;
                            cardRoot.kcm.labelFontStrikeout = false;
                        }
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Scale:")
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: fontSizeScaleSlider

                        Layout.preferredWidth: cardRoot.constants.sliderPreferredWidth
                        from: 25
                        to: 300
                        stepSize: 5
                        value: cardRoot.kcm.labelFontSizeScale * 100
                        onMoved: cardRoot.kcm.labelFontSizeScale = value / 100
                        Accessible.name: i18n("Label font size scale")
                    }

                    Label {
                        text: Math.round(fontSizeScaleSlider.value) + "%"
                        Layout.preferredWidth: cardRoot.constants.sliderValueLabelWidth
                    }

                }

            }

        }

    }

    // ═══════════════════════════════════════════════════════════════════════
    // EFFECTS CARD
    // ═══════════════════════════════════════════════════════════════════════
    Item {
        Layout.fillWidth: true
        implicitHeight: effectsCard.implicitHeight

        Kirigami.Card {
            id: effectsCard

            anchors.fill: parent
            enabled: cardRoot.kcm.snappingEnabled

            header: Kirigami.Heading {
                level: 3
                text: i18n("Effects")
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Visual Effects")
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Blur:")
                    text: i18n("Enable blur behind zones")
                    checked: cardRoot.kcm.enableBlur
                    onToggled: cardRoot.kcm.enableBlur = checked
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Numbers:")
                    text: i18n("Show zone numbers")
                    checked: cardRoot.kcm.showZoneNumbers
                    onToggled: cardRoot.kcm.showZoneNumbers = checked
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Animation:")
                    text: i18n("Flash zones when switching layouts")
                    checked: cardRoot.kcm.flashZonesOnSwitch
                    onToggled: cardRoot.kcm.flashZonesOnSwitch = checked
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Shader Effects")
                }

                CheckBox {
                    id: shaderEffectsCheck

                    Kirigami.FormData.label: i18n("Shaders:")
                    text: i18n("Enable shader effects")
                    checked: cardRoot.kcm.enableShaderEffects
                    onToggled: cardRoot.kcm.enableShaderEffects = checked
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Frame rate:")
                    enabled: shaderEffectsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: shaderFpsSlider

                        Layout.preferredWidth: cardRoot.constants.sliderPreferredWidth
                        from: 30
                        to: 144
                        stepSize: 1
                        value: cardRoot.kcm.shaderFrameRate
                        onMoved: cardRoot.kcm.shaderFrameRate = Math.round(value)
                    }

                    Label {
                        text: Math.round(shaderFpsSlider.value) + " fps"
                        Layout.preferredWidth: cardRoot.constants.sliderValueLabelWidth + 15
                    }

                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Audio Visualization")
                }

                CheckBox {
                    id: audioVizCheck

                    Kirigami.FormData.label: i18n("Audio:")
                    text: i18n("Enable CAVA audio spectrum")
                    enabled: shaderEffectsCheck.checked && cardRoot.kcm.cavaAvailable
                    checked: cardRoot.kcm.enableAudioVisualizer
                    onToggled: cardRoot.kcm.enableAudioVisualizer = checked
                    ToolTip.visible: hovered
                    ToolTip.text: cardRoot.kcm.cavaAvailable ? i18n("Feeds audio spectrum data to shaders that support it.") : i18n("CAVA is not installed. Install cava to enable audio visualization.")
                }

                Kirigami.InlineMessage {
                    Layout.fillWidth: true
                    type: Kirigami.MessageType.Warning
                    text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                    visible: !cardRoot.kcm.cavaAvailable && shaderEffectsCheck.checked
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Spectrum bars:")
                    enabled: shaderEffectsCheck.checked && audioVizCheck.checked && cardRoot.kcm.cavaAvailable
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: audioBarsSlider

                        Layout.preferredWidth: cardRoot.constants.sliderPreferredWidth
                        from: 16 // Audio::MinBars (src/core/constants.h)
                        to: 256 // Audio::MaxBars (src/core/constants.h)
                        stepSize: 2 // CAVA requires even bar count for stereo
                        value: cardRoot.kcm.audioSpectrumBarCount
                        onMoved: cardRoot.kcm.audioSpectrumBarCount = Math.round(value)
                    }

                    Label {
                        text: Math.round(audioBarsSlider.value)
                        Layout.preferredWidth: cardRoot.constants.sliderValueLabelWidth + 15
                    }

                }

            }

        }

    }

    ColorDialog {
        id: highlightColorDialog

        title: i18n("Choose Highlight Color")
        onAccepted: cardRoot.kcm.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog

        title: i18n("Choose Inactive Zone Color")
        onAccepted: cardRoot.kcm.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog

        title: i18n("Choose Border Color")
        onAccepted: cardRoot.kcm.borderColor = selectedColor
    }

    ColorDialog {
        id: labelFontColorDialog

        title: i18n("Choose Label Color")
        onAccepted: cardRoot.kcm.labelFontColor = selectedColor
    }

    FontPickerDialog {
        id: fontDialog

        kcm: cardRoot.kcm
        onAccepted: {
            cardRoot.kcm.labelFontFamily = selectedFamily;
            cardRoot.kcm.labelFontWeight = selectedWeight;
            cardRoot.kcm.labelFontItalic = selectedItalic;
            cardRoot.kcm.labelFontUnderline = selectedUnderline;
            cardRoot.kcm.labelFontStrikeout = selectedStrikeout;
        }
    }

    FileDialog {
        id: colorFileDialog

        title: i18n("Import Colors from File")
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        fileMode: FileDialog.OpenFile
        onAccepted: cardRoot.kcm.loadColorsFromFile(selectedFile.toString().replace(/^file:\/\/+/, "/"))
    }

    // Color import error dialog
    Kirigami.PromptDialog {
        id: colorImportErrorDialog

        title: i18n("Color Import Failed")
        subtitle: ""
        standardButtons: Kirigami.Dialog.Ok
        preferredWidth: Math.min(Kirigami.Units.gridUnit * 30, parent.width * 0.8)
    }

    // Connect to KCM signals for color import feedback
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

        target: cardRoot.kcm
    }

}
