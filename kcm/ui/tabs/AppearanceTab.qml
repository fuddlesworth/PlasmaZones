// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Appearance tab - Colors, opacity, border, and visual effects settings
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    // Signals for color dialog interactions (handled by main.qml)
    signal requestHighlightColorDialog()
    signal requestInactiveColorDialog()
    signal requestBorderColorDialog()
    signal requestColorFileDialog()

    clip: true
    contentWidth: availableWidth

    Kirigami.FormLayout {
        width: parent.width

        // Colors section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Colors")
        }

        CheckBox {
            id: useSystemColorsCheck
            Kirigami.FormData.label: i18n("Color scheme:")
            text: i18n("Use system accent color")
            checked: kcm.useSystemColors
            onToggled: kcm.useSystemColors = checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Highlight:")
            visible: !useSystemColorsCheck.checked
            spacing: Kirigami.Units.smallSpacing

            ColorButton {
                color: kcm.highlightColor
                onClicked: root.requestHighlightColorDialog()
            }

            Label {
                text: kcm.highlightColor.toString().toUpperCase()
                font: Kirigami.Theme.fixedWidthFont
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Inactive:")
            visible: !useSystemColorsCheck.checked
            spacing: Kirigami.Units.smallSpacing

            ColorButton {
                color: kcm.inactiveColor
                onClicked: root.requestInactiveColorDialog()
            }

            Label {
                text: kcm.inactiveColor.toString().toUpperCase()
                font: Kirigami.Theme.fixedWidthFont
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Border:")
            visible: !useSystemColorsCheck.checked
            spacing: Kirigami.Units.smallSpacing

            ColorButton {
                color: kcm.borderColor
                onClicked: root.requestBorderColorDialog()
            }

            Label {
                text: kcm.borderColor.toString().toUpperCase()
                font: Kirigami.Theme.fixedWidthFont
            }
        }

        // Pywal integration
        RowLayout {
            Kirigami.FormData.label: i18n("Import colors:")
            visible: !useSystemColorsCheck.checked
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18n("From pywal")
                icon.name: "color-management"
                onClicked: kcm.loadColorsFromPywal()
            }

            Button {
                text: i18n("From file...")
                icon.name: "document-open"
                onClicked: root.requestColorFileDialog()
            }
        }

        // Opacity section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Opacity")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Active zone:")
            spacing: Kirigami.Units.smallSpacing

            Slider {
                id: activeOpacitySlider
                Layout.preferredWidth: root.constants.sliderPreferredWidth
                from: 0
                to: root.constants.opacitySliderMax
                value: kcm.activeOpacity * root.constants.opacitySliderMax
                onMoved: kcm.activeOpacity = value / root.constants.opacitySliderMax
            }

            Label {
                text: Math.round(activeOpacitySlider.value) + "%"
                Layout.preferredWidth: root.constants.sliderValueLabelWidth
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Inactive zone:")
            spacing: Kirigami.Units.smallSpacing

            Slider {
                id: inactiveOpacitySlider
                Layout.preferredWidth: root.constants.sliderPreferredWidth
                from: 0
                to: root.constants.opacitySliderMax
                value: kcm.inactiveOpacity * root.constants.opacitySliderMax
                onMoved: kcm.inactiveOpacity = value / root.constants.opacitySliderMax
            }

            Label {
                text: Math.round(inactiveOpacitySlider.value) + "%"
                Layout.preferredWidth: root.constants.sliderValueLabelWidth
            }
        }

        // Border section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Border")
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Width:")
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                from: 0
                to: root.constants.borderWidthMax
                value: kcm.borderWidth
                onValueModified: kcm.borderWidth = value
            }

            Label {
                text: i18n("px")
            }
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Radius:")
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                from: 0
                to: root.constants.borderRadiusMax
                value: kcm.borderRadius
                onValueModified: kcm.borderRadius = value
            }

            Label {
                text: i18n("px")
            }
        }

        // Effects section
        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("Effects")
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Blur:")
            text: i18n("Enable blur behind zones")
            checked: kcm.enableBlur
            onToggled: kcm.enableBlur = checked
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Shaders:")
            text: i18n("Enable shader effects")
            checked: kcm.enableShaderEffects
            onToggled: kcm.enableShaderEffects = checked
        }

        RowLayout {
            Kirigami.FormData.label: i18n("Shader FPS:")
            enabled: kcm.enableShaderEffects
            spacing: Kirigami.Units.smallSpacing

            Slider {
                id: shaderFpsSlider
                Layout.preferredWidth: root.constants.sliderPreferredWidth
                from: 30
                to: 144
                stepSize: 1
                value: kcm.shaderFrameRate
                onMoved: kcm.shaderFrameRate = Math.round(value)
            }

            Label {
                text: Math.round(shaderFpsSlider.value) + " fps"
                Layout.preferredWidth: root.constants.sliderValueLabelWidth + 10
            }
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Numbers:")
            text: i18n("Show zone numbers")
            checked: kcm.showZoneNumbers
            onToggled: kcm.showZoneNumbers = checked
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Animation:")
            text: i18n("Flash zones when switching layouts")
            checked: kcm.flashZonesOnSwitch
            onToggled: kcm.flashZonesOnSwitch = checked
        }

        Kirigami.Separator {
            Kirigami.FormData.isSection: true
            Kirigami.FormData.label: i18n("On-Screen Display")
        }

        CheckBox {
            id: showOsdCheckbox
            Kirigami.FormData.label: i18n("Layout switch:")
            text: i18n("Show OSD when switching layouts")
            checked: kcm.showOsdOnLayoutSwitch
            onToggled: kcm.showOsdOnLayoutSwitch = checked
        }

        CheckBox {
            Kirigami.FormData.label: i18n("Keyboard navigation:")
            text: i18n("Show OSD when using keyboard navigation")
            checked: kcm.showNavigationOsd
            onToggled: kcm.showNavigationOsd = checked
        }

        ComboBox {
            Kirigami.FormData.label: i18n("OSD style:")
            enabled: showOsdCheckbox.checked
            // OsdStyle enum: 0=None, 1=Text, 2=Preview
            // ComboBox only shows Text (1) and Preview (2), so map: 1->0, 2->1
            // Use Math.max to handle edge case where osdStyle could be 0 (None)
            currentIndex: Math.max(0, kcm.osdStyle - 1)
            model: [
                i18n("Text only"),
                i18n("Visual preview")
            ]
            onActivated: (index) => {
                kcm.osdStyle = index + 1 // Convert back: 0->1 (Text), 1->2 (Preview)
            }
        }
    }
}
