// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Layout constants
    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40
    readonly property int opacitySliderMax: 100
    readonly property int borderWidthMax: 10
    readonly property int borderRadiusMax: 50
    readonly property int paddingMax: 50
    readonly property int thresholdMax: 100
    readonly property int zoneSelectorTriggerMax: 200
    readonly property int zoneSelectorPreviewWidthMin: 80
    readonly property int zoneSelectorPreviewWidthMax: 400
    readonly property int zoneSelectorPreviewHeightMin: 60
    readonly property int zoneSelectorPreviewHeightMax: 300
    readonly property int zoneSelectorGridColumnsMax: 10
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0 ? (Screen.width / Screen.height) : (16 / 9)

    contentHeight: content.implicitHeight

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Enable toggle
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Enable Zone Snapping")
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            Switch {
                checked: kcm.snappingEnabled
                onToggled: kcm.snappingEnabled = checked
                Accessible.name: i18n("Enable zone snapping")
            }

        }

        // =====================================================================
        // APPEARANCE
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: appearanceCard.implicitHeight

            Kirigami.Card {
                id: appearanceCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Appearance")
                    level: 3
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
                        checked: kcm.useSystemColors
                        onToggled: kcm.useSystemColors = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Highlight:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        Rectangle {
                            width: 32
                            height: 32
                            radius: Kirigami.Units.smallSpacing
                            color: kcm.highlightColor
                            border.color: Kirigami.Theme.disabledTextColor
                            border.width: 1

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    highlightColorDialog.selectedColor = kcm.highlightColor;
                                    highlightColorDialog.open();
                                }
                            }

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

                        Rectangle {
                            width: 32
                            height: 32
                            radius: Kirigami.Units.smallSpacing
                            color: kcm.inactiveColor
                            border.color: Kirigami.Theme.disabledTextColor
                            border.width: 1

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    inactiveColorDialog.selectedColor = kcm.inactiveColor;
                                    inactiveColorDialog.open();
                                }
                            }

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

                        Rectangle {
                            width: 32
                            height: 32
                            radius: Kirigami.Units.smallSpacing
                            color: kcm.borderColor
                            border.color: Kirigami.Theme.disabledTextColor
                            border.width: 1

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    borderColorDialog.selectedColor = kcm.borderColor;
                                    borderColorDialog.open();
                                }
                            }

                        }

                        Label {
                            text: kcm.borderColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
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

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0
                            to: root.opacitySliderMax
                            value: kcm.activeOpacity * root.opacitySliderMax
                            onMoved: kcm.activeOpacity = value / root.opacitySliderMax
                            Accessible.name: i18n("Active zone opacity")
                        }

                        Label {
                            text: Math.round(activeOpacitySlider.value) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Inactive opacity:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: inactiveOpacitySlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0
                            to: root.opacitySliderMax
                            value: kcm.inactiveOpacity * root.opacitySliderMax
                            onMoved: kcm.inactiveOpacity = value / root.opacitySliderMax
                            Accessible.name: i18n("Inactive zone opacity")
                        }

                        Label {
                            text: Math.round(inactiveOpacitySlider.value) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
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
                            to: root.borderWidthMax
                            value: kcm.borderWidth
                            onValueModified: kcm.borderWidth = value
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
                            to: root.borderRadiusMax
                            value: kcm.borderRadius
                            onValueModified: kcm.borderRadius = value
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

                        Rectangle {
                            width: 32
                            height: 32
                            radius: Kirigami.Units.smallSpacing
                            color: kcm.labelFontColor
                            border.color: Kirigami.Theme.disabledTextColor
                            border.width: 1

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    labelFontColorDialog.selectedColor = kcm.labelFontColor;
                                    labelFontColorDialog.open();
                                }
                            }

                        }

                        Label {
                            text: kcm.labelFontColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Font:")
                        spacing: Kirigami.Units.smallSpacing

                        TextField {
                            Layout.preferredWidth: root.sliderPreferredWidth
                            text: kcm.labelFontFamily
                            placeholderText: i18n("System default")
                            onEditingFinished: kcm.labelFontFamily = text
                        }

                        Button {
                            icon.name: "edit-clear"
                            visible: kcm.labelFontFamily !== "" || kcm.labelFontWeight !== Font.Bold || kcm.labelFontItalic || kcm.labelFontUnderline || kcm.labelFontStrikeout || Math.abs(kcm.labelFontSizeScale - 1) > 0.01
                            ToolTip.text: i18n("Reset to defaults")
                            ToolTip.visible: hovered
                            onClicked: {
                                kcm.labelFontFamily = "";
                                kcm.labelFontSizeScale = 1;
                                kcm.labelFontWeight = Font.Bold;
                                kcm.labelFontItalic = false;
                                kcm.labelFontUnderline = false;
                                kcm.labelFontStrikeout = false;
                            }
                        }

                    }

                    ComboBox {
                        id: fontWeightCombo

                        Kirigami.FormData.label: i18n("Weight:")
                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Thin"),
                            "value": Font.Thin
                        }, {
                            "text": i18n("Light"),
                            "value": Font.Light
                        }, {
                            "text": i18n("Normal"),
                            "value": Font.Normal
                        }, {
                            "text": i18n("Medium"),
                            "value": Font.Medium
                        }, {
                            "text": i18n("DemiBold"),
                            "value": Font.DemiBold
                        }, {
                            "text": i18n("Bold"),
                            "value": Font.Bold
                        }, {
                            "text": i18n("ExtraBold"),
                            "value": Font.ExtraBold
                        }, {
                            "text": i18n("Black"),
                            "value": Font.Black
                        }]
                        currentIndex: {
                            for (var i = 0; i < model.length; i++) {
                                if (model[i].value === kcm.labelFontWeight)
                                    return i;

                            }
                            return 5; // Bold default
                        }
                        onActivated: kcm.labelFontWeight = currentValue
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Style:")
                        spacing: Kirigami.Units.largeSpacing

                        CheckBox {
                            text: i18n("Italic")
                            checked: kcm.labelFontItalic
                            onToggled: kcm.labelFontItalic = checked
                        }

                        CheckBox {
                            text: i18n("Underline")
                            checked: kcm.labelFontUnderline
                            onToggled: kcm.labelFontUnderline = checked
                        }

                        CheckBox {
                            text: i18n("Strikeout")
                            checked: kcm.labelFontStrikeout
                            onToggled: kcm.labelFontStrikeout = checked
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Scale:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: fontSizeScaleSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 25
                            to: 300
                            stepSize: 5
                            value: kcm.labelFontSizeScale * 100
                            onMoved: kcm.labelFontSizeScale = value / 100
                            Accessible.name: i18n("Label font size scale")
                        }

                        Label {
                            text: Math.round(fontSizeScaleSlider.value) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
                        }

                    }

                }

            }

        }

        // =====================================================================
        // EFFECTS
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: effectsCard.implicitHeight

            Kirigami.Card {
                id: effectsCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Effects")
                    level: 3
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

                }

            }

        }

        // =====================================================================
        // SHADER EFFECTS
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: shaderCard.implicitHeight

            Kirigami.Card {
                id: shaderCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Shader Effects")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
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
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: shaderFpsSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 30
                            to: 144
                            stepSize: 1
                            value: kcm.shaderFrameRate
                            onMoved: kcm.shaderFrameRate = Math.round(value)
                        }

                        Label {
                            text: Math.round(shaderFpsSlider.value) + " fps"
                            Layout.preferredWidth: root.sliderValueLabelWidth + 15
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
                        enabled: shaderEffectsCheck.checked
                        checked: kcm.enableAudioVisualizer
                        onToggled: kcm.enableAudioVisualizer = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Feeds audio spectrum data to shaders that support it.")
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Spectrum bars:")
                        enabled: shaderEffectsCheck.checked && audioVizCheck.checked
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: audioBarsSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 16
                            to: 256
                            stepSize: 2
                            value: kcm.audioSpectrumBarCount
                            onMoved: kcm.audioSpectrumBarCount = Math.round(value)
                        }

                        Label {
                            text: Math.round(audioBarsSlider.value)
                            Layout.preferredWidth: root.sliderValueLabelWidth + 15
                        }

                    }

                }

            }

        }

        // =====================================================================
        // ACTIVATION
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: activationCard.implicitHeight

            Kirigami.Card {
                id: activationCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Activation")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Triggers")
                    }

                    CheckBox {
                        id: alwaysActivateCheck

                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Zone activation:")
                        text: i18n("Activate on every window drag")
                        checked: true // alwaysActivateOnDrag is derived from drag trigger config
                        enabled: false // Read-only display — configure via drag triggers above
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, the zone overlay appears on every window drag without requiring a modifier key or mouse button.")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Toggle mode:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.6
                        text: i18n("Tap trigger to toggle overlay")
                        checked: kcm.toggleActivation
                        onToggled: kcm.toggleActivation = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, press the activation trigger once to show the overlay, press again to hide it. When disabled, hold the trigger to show.")
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Zone Span")
                    }

                    CheckBox {
                        id: zoneSpanEnabledCheck

                        Kirigami.FormData.label: i18n("Paint-to-span:")
                        text: i18n("Enable zone spanning")
                        checked: kcm.zoneSpanEnabled
                        onToggled: kcm.zoneSpanEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, you can paint across multiple zones to snap a window to the combined area.")
                    }

                    RowLayout {
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Edge threshold:")
                        enabled: zoneSpanEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            id: adjacentThresholdSpinBox

                            from: 5
                            to: root.thresholdMax
                            value: kcm.adjacentThreshold
                            onValueModified: kcm.adjacentThreshold = value
                            Accessible.name: i18n("Edge threshold")
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Distance from zone edge for multi-zone selection")
                        }

                        Label {
                            text: i18n("px")
                        }

                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Snap Assist")
                    }

                    CheckBox {
                        id: snapAssistFeatureEnabledCheck

                        Kirigami.FormData.label: i18n("Window picker:")
                        text: i18n("Enable snap assist")
                        checked: kcm.snapAssistFeatureEnabled
                        onToggled: kcm.snapAssistFeatureEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Show a window picker after snapping to fill remaining empty zones")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Behavior:")
                        text: i18n("Always show after snapping")
                        checked: kcm.snapAssistEnabled
                        onToggled: kcm.snapAssistEnabled = checked
                        enabled: snapAssistFeatureEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, a window picker appears after every snap. When disabled, hold the trigger below while dropping to show the picker for that snap only.")
                    }

                }

            }

        }

        // =====================================================================
        // BEHAVIOR
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: behaviorCard.implicitHeight

            Kirigami.Card {
                id: behaviorCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Behavior")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Display:")
                        text: i18n("Show zones on all monitors while dragging")
                        checked: kcm.showZonesOnAllMonitors
                        onToggled: kcm.showZonesOnAllMonitors = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Resolution:")
                        text: i18n("Re-snap windows to their zones after resolution changes")
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
                        Kirigami.FormData.label: i18n("Reopening:")
                        text: i18n("Restore windows to their previous zone")
                        checked: kcm.restoreWindowsToZonesOnLogin
                        onToggled: kcm.restoreWindowsToZonesOnLogin = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, windows return to their previous zone when reopened, including after login or session restart.")
                    }

                    WideComboBox {
                        id: stickyHandlingCombo

                        Kirigami.FormData.label: i18n("Sticky windows:")
                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Treat as normal"),
                            "value": 0
                        }, {
                            "text": i18n("Restore only"),
                            "value": 1
                        }, {
                            "text": i18n("Ignore all"),
                            "value": 2
                        }]
                        currentIndex: Math.max(0, indexOfValue(kcm.stickyWindowHandling))
                        onActivated: kcm.stickyWindowHandling = currentValue
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
                    }

                }

            }

        }

        // =====================================================================
        // ZONE GEOMETRY
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: gapsCard.implicitHeight

            Kirigami.Card {
                id: gapsCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Gaps")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    RowLayout {
                        Kirigami.FormData.label: i18n("Zone padding:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: kcm.zonePadding
                            onValueModified: kcm.zonePadding = value
                            Accessible.name: i18n("Zone padding")
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
                            to: root.paddingMax
                            value: kcm.outerGap
                            onValueModified: kcm.outerGap = value
                            enabled: !perSideCheck.checked
                            Accessible.name: i18n("Edge gap")
                        }

                        Label {
                            text: i18n("px")
                            visible: !perSideCheck.checked
                        }

                        CheckBox {
                            id: perSideCheck

                            text: i18n("Set per side")
                            checked: kcm.usePerSideOuterGap
                            onToggled: kcm.usePerSideOuterGap = checked
                        }

                    }

                    GridLayout {
                        Kirigami.FormData.label: i18n("Per-side gaps:")
                        visible: perSideCheck.checked
                        columns: 6
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Top:")
                        }

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: kcm.outerGapTop
                            onValueModified: kcm.outerGapTop = value
                            Accessible.name: i18nc("@label", "Top edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                        Label {
                            text: i18n("Bottom:")
                        }

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: kcm.outerGapBottom
                            onValueModified: kcm.outerGapBottom = value
                            Accessible.name: i18nc("@label", "Bottom edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                        Label {
                            text: i18n("Left:")
                        }

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: kcm.outerGapLeft
                            onValueModified: kcm.outerGapLeft = value
                            Accessible.name: i18nc("@label", "Left edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                        Label {
                            text: i18n("Right:")
                        }

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: kcm.outerGapRight
                            onValueModified: kcm.outerGapRight = value
                            Accessible.name: i18nc("@label", "Right edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                    }

                }

            }

        }

        // =====================================================================
        // ZONE SELECTOR
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: zoneSelectorCard.implicitHeight

            Kirigami.Card {
                id: zoneSelectorCard

                anchors.fill: parent
                enabled: kcm.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Zone Selector")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: zoneSelectorEnabledCheck

                        Kirigami.FormData.label: i18n("Popup:")
                        text: i18n("Enable zone selector popup")
                        checked: kcm.zoneSelectorEnabled
                        onToggled: kcm.zoneSelectorEnabled = checked
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Position & Trigger")
                    }

                    // 3x3 position picker grid (matching KCM PositionPicker)
                    Item {
                        readonly property var positionLabels: [i18n("Top-Left"), i18n("Top"), i18n("Top-Right"), i18n("Left"), i18n("Center"), i18n("Right"), i18n("Bottom-Left"), i18n("Bottom"), i18n("Bottom-Right")]

                        Kirigami.FormData.label: i18n("Position:")
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        implicitWidth: 160
                        implicitHeight: 130

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 4

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: Kirigami.Theme.backgroundColor
                                radius: Kirigami.Units.smallSpacing
                                border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
                                border.width: 1

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 4
                                    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.05)
                                    radius: Kirigami.Units.smallSpacing / 2

                                    Grid {
                                        id: positionGrid

                                        anchors.fill: parent
                                        anchors.margins: 6
                                        columns: 3
                                        rows: 3
                                        spacing: 3

                                        Repeater {
                                            model: 9

                                            Rectangle {
                                                id: posCell

                                                required property int index
                                                property bool isSelected: index === kcm.zoneSelectorPosition
                                                property bool isHovered: posCellMouse.containsMouse

                                                width: (positionGrid.width - positionGrid.spacing * 2) / 3
                                                height: (positionGrid.height - positionGrid.spacing * 2) / 3
                                                radius: 3
                                                color: {
                                                    if (isSelected)
                                                        return Kirigami.Theme.highlightColor;

                                                    if (isHovered && zoneSelectorEnabledCheck.checked)
                                                        return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4);

                                                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15);
                                                }
                                                border.color: {
                                                    if (isSelected)
                                                        return Kirigami.Theme.highlightColor;

                                                    if (isHovered && zoneSelectorEnabledCheck.checked)
                                                        return Kirigami.Theme.highlightColor;

                                                    return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3);
                                                }
                                                border.width: isSelected ? 2 : 1

                                                MouseArea {
                                                    id: posCellMouse

                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    enabled: zoneSelectorEnabledCheck.checked
                                                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                                    onClicked: kcm.zoneSelectorPosition = posCell.index
                                                    ToolTip.visible: containsMouse && zoneSelectorEnabledCheck.checked
                                                    ToolTip.delay: 500
                                                    ToolTip.text: positionLabels[posCell.index]
                                                }

                                                Behavior on color {
                                                    ColorAnimation {
                                                        duration: 100
                                                    }

                                                }

                                            }

                                        }

                                    }

                                }

                            }

                            Label {
                                Layout.alignment: Qt.AlignHCenter
                                text: positionLabels[kcm.zoneSelectorPosition] || ""
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                opacity: 0.7
                            }

                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Trigger distance:")
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: triggerDistanceSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 10
                            to: root.zoneSelectorTriggerMax
                            stepSize: 10
                            value: kcm.zoneSelectorTriggerDistance
                            onMoved: kcm.zoneSelectorTriggerDistance = value
                        }

                        Label {
                            text: Math.round(triggerDistanceSlider.value) + " px"
                            Layout.preferredWidth: root.sliderValueLabelWidth + 15
                        }

                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Layout Arrangement")
                    }

                    ComboBox {
                        id: layoutModeCombo

                        Kirigami.FormData.label: i18n("Arrangement:")
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Grid"),
                            "value": 0
                        }, {
                            "text": i18n("Horizontal"),
                            "value": 1
                        }, {
                            "text": i18n("Vertical"),
                            "value": 2
                        }]
                        currentIndex: Math.max(0, indexOfValue(kcm.zoneSelectorLayoutMode))
                        onActivated: kcm.zoneSelectorLayoutMode = currentValue
                    }

                    SpinBox {
                        Kirigami.FormData.label: i18n("Grid columns:")
                        visible: kcm.zoneSelectorLayoutMode === 0
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        from: 1
                        to: root.zoneSelectorGridColumnsMax
                        value: kcm.zoneSelectorGridColumns
                        onValueModified: kcm.zoneSelectorGridColumns = value
                    }

                    SpinBox {
                        Kirigami.FormData.label: i18n("Max visible rows:")
                        visible: kcm.zoneSelectorLayoutMode === 0
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        from: 1
                        to: 10
                        value: kcm.zoneSelectorMaxRows
                        onValueModified: kcm.zoneSelectorMaxRows = value
                    }

                    Kirigami.Separator {
                        Kirigami.FormData.isSection: true
                        Kirigami.FormData.label: i18n("Preview Size")
                    }

                    // Live preview
                    Item {
                        readonly property int effectivePreviewWidth: {
                            if (kcm.zoneSelectorSizeMode === 0)
                                return Math.round(180 * (root.screenAspectRatio / (16 / 9)));

                            return kcm.zoneSelectorPreviewWidth;
                        }
                        readonly property int effectivePreviewHeight: {
                            if (kcm.zoneSelectorSizeMode === 0)
                                return Math.round(effectivePreviewWidth / root.screenAspectRatio);

                            return kcm.zoneSelectorPreviewHeight;
                        }

                        Layout.fillWidth: true
                        Layout.preferredHeight: effectivePreviewHeight + 50
                        visible: zoneSelectorEnabledCheck.checked
                        enabled: zoneSelectorEnabledCheck.checked

                        Item {
                            id: previewContainer

                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            width: parent.effectivePreviewWidth
                            height: parent.effectivePreviewHeight

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                                radius: Kirigami.Units.smallSpacing * 1.5
                                border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)
                                border.width: 1

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: 2
                                    spacing: 1

                                    Repeater {
                                        model: 3

                                        Rectangle {
                                            width: (parent.width - 2) / 3
                                            height: parent.height
                                            radius: 2
                                            color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.35)
                                            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.7)
                                            border.width: 1

                                            Label {
                                                anchors.centerIn: parent
                                                text: (index + 1).toString()
                                                font.pixelSize: Math.min(parent.width, parent.height) * 0.3
                                                font.bold: true
                                                opacity: 0.6
                                                visible: parent.width >= 20
                                            }

                                        }

                                    }

                                }

                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.bottom
                                anchors.topMargin: Kirigami.Units.smallSpacing
                                text: parent.width + " \u00d7 " + parent.height + " px"
                                font: Kirigami.Theme.fixedWidthFont
                                opacity: 0.7
                            }

                        }

                    }

                    // Size preset buttons (matching KCM segmented style)
                    RowLayout {
                        id: sizeButtonRow

                        property bool customModeActive: false
                        property int selectedSize: {
                            if (kcm.zoneSelectorSizeMode === 0)
                                return 0;

                            if (customModeActive)
                                return 4;

                            var w = kcm.zoneSelectorPreviewWidth;
                            if (Math.abs(w - 120) <= 5)
                                return 1;

                            if (Math.abs(w - 180) <= 5)
                                return 2;

                            if (Math.abs(w - 260) <= 5)
                                return 3;

                            return 4;
                        }

                        Kirigami.FormData.label: i18n("Size:")
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        spacing: 0

                        Button {
                            text: i18n("Auto")
                            flat: sizeButtonRow.selectedSize !== 0
                            highlighted: sizeButtonRow.selectedSize === 0
                            onClicked: {
                                sizeButtonRow.customModeActive = false;
                                kcm.zoneSelectorSizeMode = 0;
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("Approximately 10% of screen width (120-280px)")
                        }

                        Button {
                            text: i18n("Small")
                            flat: sizeButtonRow.selectedSize !== 1
                            highlighted: sizeButtonRow.selectedSize === 1
                            onClicked: {
                                sizeButtonRow.customModeActive = false;
                                kcm.zoneSelectorSizeMode = 1;
                                kcm.zoneSelectorPreviewWidth = 120;
                                kcm.zoneSelectorPreviewHeight = Math.round(120 / root.screenAspectRatio);
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("120px width")
                        }

                        Button {
                            text: i18n("Medium")
                            flat: sizeButtonRow.selectedSize !== 2
                            highlighted: sizeButtonRow.selectedSize === 2
                            onClicked: {
                                sizeButtonRow.customModeActive = false;
                                kcm.zoneSelectorSizeMode = 1;
                                kcm.zoneSelectorPreviewWidth = 180;
                                kcm.zoneSelectorPreviewHeight = Math.round(180 / root.screenAspectRatio);
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("180px width")
                        }

                        Button {
                            text: i18n("Large")
                            flat: sizeButtonRow.selectedSize !== 3
                            highlighted: sizeButtonRow.selectedSize === 3
                            onClicked: {
                                sizeButtonRow.customModeActive = false;
                                kcm.zoneSelectorSizeMode = 1;
                                kcm.zoneSelectorPreviewWidth = 260;
                                kcm.zoneSelectorPreviewHeight = Math.round(260 / root.screenAspectRatio);
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("260px width")
                        }

                        Button {
                            text: i18n("Custom")
                            flat: sizeButtonRow.selectedSize !== 4
                            highlighted: sizeButtonRow.selectedSize === 4
                            onClicked: {
                                sizeButtonRow.customModeActive = true;
                                kcm.zoneSelectorSizeMode = 1;
                            }
                            ToolTip.visible: hovered
                            ToolTip.delay: Kirigami.Units.toolTipDelay
                            ToolTip.text: i18n("Custom size with slider")
                        }

                    }

                    // Custom size slider
                    RowLayout {
                        Kirigami.FormData.label: i18n("Width:")
                        visible: sizeButtonRow.selectedSize === 4
                        enabled: zoneSelectorEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: customSizeSlider

                            Layout.fillWidth: true
                            from: root.zoneSelectorPreviewWidthMin
                            to: root.zoneSelectorPreviewWidthMax
                            stepSize: 10
                            value: kcm.zoneSelectorPreviewWidth
                            Accessible.name: i18n("Preview size")
                            onMoved: {
                                kcm.zoneSelectorPreviewWidth = value;
                                var newHeight = Math.round(value / root.screenAspectRatio);
                                newHeight = Math.max(root.zoneSelectorPreviewHeightMin, Math.min(root.zoneSelectorPreviewHeightMax, newHeight));
                                kcm.zoneSelectorPreviewHeight = newHeight;
                            }
                        }

                        Label {
                            text: kcm.zoneSelectorPreviewWidth + " px"
                            Layout.preferredWidth: 55
                            horizontalAlignment: Text.AlignRight
                            font: Kirigami.Theme.fixedWidthFont
                        }

                    }

                    // Info text for auto mode
                    Label {
                        Layout.fillWidth: true
                        visible: kcm.zoneSelectorSizeMode === 0
                        text: i18n("Preview size adjusts automatically based on your screen resolution.")
                        opacity: 0.6
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                        wrapMode: Text.WordWrap
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
        onAccepted: kcm.highlightColor = selectedColor
    }

    ColorDialog {
        id: inactiveColorDialog

        title: i18n("Choose Inactive Zone Color")
        onAccepted: kcm.inactiveColor = selectedColor
    }

    ColorDialog {
        id: borderColorDialog

        title: i18n("Choose Border Color")
        onAccepted: kcm.borderColor = selectedColor
    }

    ColorDialog {
        id: labelFontColorDialog

        title: i18n("Choose Label Color")
        onAccepted: kcm.labelFontColor = selectedColor
    }

}
