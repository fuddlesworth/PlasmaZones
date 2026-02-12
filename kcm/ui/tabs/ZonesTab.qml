// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import ".."

/**
 * @brief Zones tab - Consolidated appearance and behavior settings
 *
 * Merges the former Appearance and Behavior tabs into a unified card-based layout
 * for consistent UX across the KCM.
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants

    // Whether this tab is currently visible (for conditional tooltips)
    property bool isCurrentTab: false

    // Signals for color dialog interactions (handled by main.qml)
    signal requestHighlightColorDialog()
    signal requestInactiveColorDialog()
    signal requestBorderColorDialog()
    signal requestLabelFontColorDialog()
    signal requestFontDialog()
    signal requestColorFileDialog()

    clip: true
    contentWidth: availableWidth

    ColumnLayout {
        width: parent.width
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

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Appearance")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    // Colors subsection
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

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    // Opacity subsection
                    RowLayout {
                        Kirigami.FormData.label: i18n("Active opacity:")
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
                        Kirigami.FormData.label: i18n("Inactive opacity:")
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

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                    }

                    // Border subsection
                    RowLayout {
                        Kirigami.FormData.label: i18n("Border width:")
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
                        Kirigami.FormData.label: i18n("Border radius:")
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

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Zone Labels")
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Color:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.labelFontColor
                            onClicked: root.requestLabelFontColorDialog()
                        }

                        Label {
                            text: kcm.labelFontColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Font:")
                        spacing: Kirigami.Units.smallSpacing

                        Button {
                            text: kcm.labelFontFamily || i18n("System default")
                            font.family: kcm.labelFontFamily
                            font.weight: kcm.labelFontWeight
                            font.italic: kcm.labelFontItalic
                            icon.name: "font-select-symbolic"
                            onClicked: root.requestFontDialog()
                        }

                        Button {
                            icon.name: "edit-clear"
                            visible: kcm.labelFontFamily !== ""
                                || kcm.labelFontWeight !== Font.Bold
                                || kcm.labelFontItalic
                                || kcm.labelFontUnderline
                                || kcm.labelFontStrikeout
                                || Math.abs(kcm.labelFontSizeScale - 1.0) > 0.01
                            ToolTip.text: i18n("Reset to defaults")
                            ToolTip.visible: hovered
                            onClicked: {
                                kcm.labelFontFamily = ""
                                kcm.labelFontSizeScale = 1.0
                                kcm.labelFontWeight = Font.Bold
                                kcm.labelFontItalic = false
                                kcm.labelFontUnderline = false
                                kcm.labelFontStrikeout = false
                            }
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Scale:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: fontSizeScaleSlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 25
                            to: 300
                            stepSize: 5
                            value: kcm.labelFontSizeScale * 100
                            onMoved: kcm.labelFontSizeScale = value / 100
                        }

                        Label {
                            text: Math.round(fontSizeScaleSlider.value) + "%"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth
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

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Effects")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    // Visual effects
                    CheckBox {
                        Kirigami.FormData.label: i18n("Blur:")
                        text: i18n("Enable blur behind zones")
                        checked: kcm.enableBlur
                        onToggled: kcm.enableBlur = checked
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
                        Kirigami.FormData.label: i18n("Shader Effects")
                    }

                    CheckBox {
                        id: shaderEffectsCheck
                        Kirigami.FormData.label: i18n("Shaders:")
                        text: i18n("Enable shader effects")
                        checked: kcm.enableShaderEffects
                        onToggled: kcm.enableShaderEffects = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Frame rate:")
                        enabled: shaderEffectsCheck.checked
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
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
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
                        enabled: shaderEffectsCheck.checked && kcm.cavaAvailable
                        checked: kcm.enableAudioVisualizer
                        onToggled: kcm.enableAudioVisualizer = checked

                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: kcm.cavaAvailable
                            ? i18n("Feeds audio spectrum data to shaders that support it.")
                            : i18n("CAVA is not installed. Install cava to enable audio visualization.")
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Warning
                        text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                        visible: !kcm.cavaAvailable && shaderEffectsCheck.checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Spectrum bars:")
                        enabled: shaderEffectsCheck.checked && audioVizCheck.checked && kcm.cavaAvailable
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: audioBarsSlider
                            Layout.preferredWidth: root.constants.sliderPreferredWidth
                            from: 16
                            to: 256
                            stepSize: 1
                            value: kcm.audioSpectrumBarCount
                            onMoved: kcm.audioSpectrumBarCount = Math.round(value)
                        }

                        Label {
                            text: Math.round(audioBarsSlider.value)
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth + 15
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // ACTIVATION CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: activationCard.implicitHeight

            Kirigami.Card {
                id: activationCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Activation")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    ModifierAndMouseCheckBoxes {
                        id: dragActivationInput
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.constants.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Zone activation:")
                        acceptMode: ModifierAndMouseCheckBoxes.acceptModeAll
                        modifierValue: kcm.dragActivationModifier
                        mouseButtonValue: kcm.dragActivationMouseButton
                        defaultModifierValue: kcm.defaultDragActivationModifier
                        defaultMouseButtonValue: kcm.defaultDragActivationMouseButton
                        tooltipEnabled: root.isCurrentTab
                        customTooltipText: i18n("Hold modifier or use this mouse button to show zones while dragging (e.g. left-click to drag, another button to activate zones)")
                        onValueModified: (value) => {
                            kcm.dragActivationModifier = value
                        }
                        onMouseButtonsModified: (value) => {
                            kcm.dragActivationMouseButton = value
                        }
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Multi-Zone Selection")
                    }

                    CheckBox {
                        id: proximitySnapAlwaysOnCheckBox
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Proximity snap:")
                        text: i18n("Proximity snap always active")
                        checked: kcm.proximitySnapAlwaysOn
                        onCheckedChanged: {
                            if (checked !== kcm.proximitySnapAlwaysOn) {
                                kcm.proximitySnapAlwaysOn = checked
                            }
                        }
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("Proximity snap is always active during zone selection — no need to hold the modifier key")
                    }

                    ModifierAndMouseCheckBoxes {
                        id: multiZoneModifiers
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.constants.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Proximity snap modifier:")
                        visible: !kcm.proximitySnapAlwaysOn
                        acceptMode: ModifierAndMouseCheckBoxes.acceptModeMetaOnly
                        modifierValue: kcm.multiZoneModifier
                        defaultModifierValue: kcm.defaultMultiZoneModifier
                        tooltipEnabled: root.isCurrentTab
                        customTooltipText: i18n("Hold this modifier while dragging to snap to adjacent zones detected by edge proximity")
                        onValueModified: (value) => {
                            kcm.multiZoneModifier = value
                        }
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.constants.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Paint-to-span modifier:")
                        acceptMode: ModifierAndMouseCheckBoxes.acceptModeMetaOnly
                        modifierValue: kcm.zoneSpanModifier
                        defaultModifierValue: kcm.defaultZoneSpanModifier
                        tooltipEnabled: root.isCurrentTab
                        customTooltipText: i18n("Hold this modifier while dragging to paint across zones — each zone the cursor enters is added to the selection")
                        onValueModified: (value) => {
                            kcm.zoneSpanModifier = value
                        }
                    }

                    RowLayout {
                        Layout.preferredWidth: root.constants.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Edge threshold:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            id: adjacentThresholdSpinBox
                            from: 0
                            to: root.constants.thresholdMax
                            value: kcm.adjacentThreshold
                            onValueModified: kcm.adjacentThreshold = value

                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Distance from zone edge for multi-zone selection")
                        }

                        Label {
                            text: i18n("px")
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // ZONE LAYOUT CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: zoneLayoutCard.implicitHeight

            Kirigami.Card {
                id: zoneLayoutCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Zone Layout")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    RowLayout {
                        Kirigami.FormData.label: i18n("Zone padding:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.constants.thresholdMax
                            value: kcm.zonePadding
                            onValueModified: kcm.zonePadding = value
                        }

                        Label {
                            text: i18n("px")
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Edge gap:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.constants.thresholdMax
                            value: kcm.outerGap
                            onValueModified: kcm.outerGap = value
                        }

                        Label {
                            text: i18n("px")
                        }
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Display:")
                        text: i18n("Show zones on all monitors while dragging")
                        checked: kcm.showZonesOnAllMonitors
                        onToggled: kcm.showZonesOnAllMonitors = checked
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════════
        // WINDOW BEHAVIOR CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: windowBehaviorCard.implicitHeight

            Kirigami.Card {
                id: windowBehaviorCard
                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Window Behavior")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Resolution:")
                        text: i18n("Keep windows in zones when resolution changes")
                        checked: kcm.keepWindowsInZonesOnResolutionChange
                        onToggled: kcm.keepWindowsInZonesOnResolutionChange = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("New windows:")
                        text: i18n("Move new windows to their last used zone")
                        checked: kcm.moveNewWindowsToLastZone
                        onToggled: kcm.moveNewWindowsToLastZone = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Unsnapping:")
                        text: i18n("Restore original window size when unsnapping")
                        checked: kcm.restoreOriginalSizeOnUnsnap
                        onToggled: kcm.restoreOriginalSizeOnUnsnap = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Session:")
                        text: i18n("Restore windows to their zones after login")
                        checked: kcm.restoreWindowsToZonesOnLogin
                        onToggled: kcm.restoreWindowsToZonesOnLogin = checked
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("When enabled, windows return to their previous zones after logging in or restarting the session.")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Snap Assist:")
                        text: i18n("Show window picker after snapping to fill empty zones")
                        checked: kcm.snapAssistEnabled
                        onToggled: kcm.snapAssistEnabled = checked
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("When enabled, after snapping a window you can pick another window to fill the remaining empty zones.")
                    }

                    ComboBox {
                        id: stickyHandlingCombo
                        Kirigami.FormData.label: i18n("Sticky windows:")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: i18n("Treat as normal"), value: 0 },
                            { text: i18n("Restore only"), value: 1 },
                            { text: i18n("Ignore all"), value: 2 }
                        ]
                        currentIndex: indexForValue(kcm.stickyWindowHandling)
                        onActivated: kcm.stickyWindowHandling = currentValue

                        function indexForValue(value) {
                            for (let i = 0; i < model.length; i++) {
                                if (model[i].value === value) return i
                            }
                            return 0
                        }

                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
                    }
                }
            }
        }
    }
}
