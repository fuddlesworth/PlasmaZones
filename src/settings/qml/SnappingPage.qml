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
    // Per-screen snapping gap/padding helper
    property alias selectedSnappingScreenName: snappingHelper.selectedScreenName
    readonly property alias isPerScreenSnapping: snappingHelper.isPerScreen
    readonly property alias hasSnappingOverrides: snappingHelper.hasOverrides

    function snappingSettingValue(key, globalValue) {
        return snappingHelper.settingValue(key, globalValue);
    }

    function writeSnappingSetting(key, value, globalSetter) {
        snappingHelper.writeSetting(key, value, globalSetter);
    }

    function clearSnappingOverrides() {
        snappingHelper.clearOverrides();
    }

    contentHeight: content.implicitHeight

    PerScreenOverrideHelper {
        id: snappingHelper

        appSettings: settingsController
        getterMethod: "getPerScreenSnappingSettings"
        setterMethod: "setPerScreenSnappingSetting"
        clearerMethod: "clearPerScreenSnappingSettings"
    }

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
                checked: appSettings.snappingEnabled
                onToggled: appSettings.snappingEnabled = checked
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
                enabled: appSettings.snappingEnabled

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
                        checked: appSettings.useSystemColors
                        onToggled: appSettings.useSystemColors = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Highlight:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: appSettings.highlightColor
                            onClicked: {
                                highlightColorDialog.selectedColor = appSettings.highlightColor;
                                highlightColorDialog.open();
                            }
                        }

                        Label {
                            text: appSettings.highlightColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Inactive:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: appSettings.inactiveColor
                            onClicked: {
                                inactiveColorDialog.selectedColor = appSettings.inactiveColor;
                                inactiveColorDialog.open();
                            }
                        }

                        Label {
                            text: appSettings.inactiveColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Border:")
                        visible: !useSystemColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: appSettings.borderColor
                            onClicked: {
                                borderColorDialog.selectedColor = appSettings.borderColor;
                                borderColorDialog.open();
                            }
                        }

                        Label {
                            text: appSettings.borderColor.toString().toUpperCase()
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
                    RowLayout {
                        Kirigami.FormData.label: i18n("Active opacity:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: activeOpacitySlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 0
                            to: root.opacitySliderMax
                            value: appSettings.activeOpacity * root.opacitySliderMax
                            onMoved: appSettings.activeOpacity = value / root.opacitySliderMax
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
                            value: appSettings.inactiveOpacity * root.opacitySliderMax
                            onMoved: appSettings.inactiveOpacity = value / root.opacitySliderMax
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
                            value: appSettings.borderWidth
                            onValueModified: appSettings.borderWidth = value
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
                            value: appSettings.borderRadius
                            onValueModified: appSettings.borderRadius = value
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
                            color: appSettings.labelFontColor
                            onClicked: {
                                labelFontColorDialog.selectedColor = appSettings.labelFontColor;
                                labelFontColorDialog.open();
                            }
                        }

                        Label {
                            text: appSettings.labelFontColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
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

                    RowLayout {
                        Kirigami.FormData.label: i18n("Scale:")
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: fontSizeScaleSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 25
                            to: 300
                            stepSize: 5
                            value: appSettings.labelFontSizeScale * 100
                            onMoved: appSettings.labelFontSizeScale = value / 100
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
                enabled: appSettings.snappingEnabled

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
                        checked: appSettings.enableBlur
                        onToggled: appSettings.enableBlur = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Numbers:")
                        text: i18n("Show zone numbers")
                        checked: appSettings.showZoneNumbers
                        onToggled: appSettings.showZoneNumbers = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Animation:")
                        text: i18n("Flash zones when switching layouts")
                        checked: appSettings.flashZonesOnSwitch
                        onToggled: appSettings.flashZonesOnSwitch = checked
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
                enabled: appSettings.snappingEnabled

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
                        checked: appSettings.enableShaderEffects
                        onToggled: appSettings.enableShaderEffects = checked
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
                            value: appSettings.shaderFrameRate
                            onMoved: appSettings.shaderFrameRate = Math.round(value)
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
                        enabled: shaderEffectsCheck.checked && settingsController.cavaAvailable
                        checked: appSettings.enableAudioVisualizer
                        onToggled: appSettings.enableAudioVisualizer = checked
                        ToolTip.visible: hovered
                        ToolTip.text: settingsController.cavaAvailable ? i18n("Feeds audio spectrum data to shaders that support it.") : i18n("CAVA is not installed. Install cava to enable audio visualization.")
                    }

                    Kirigami.InlineMessage {
                        Layout.fillWidth: true
                        type: Kirigami.MessageType.Warning
                        text: i18n("CAVA is not installed. Install the <b>cava</b> package to enable audio-reactive shader effects.")
                        visible: !settingsController.cavaAvailable && shaderEffectsCheck.checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Spectrum bars:")
                        enabled: shaderEffectsCheck.checked && audioVizCheck.checked && settingsController.cavaAvailable
                        opacity: enabled ? 1 : 0.6
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: audioBarsSlider

                            Layout.preferredWidth: root.sliderPreferredWidth
                            from: 16
                            to: 256
                            stepSize: 2
                            value: appSettings.audioSpectrumBarCount
                            onMoved: appSettings.audioSpectrumBarCount = Math.round(value)
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
                enabled: appSettings.snappingEnabled

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
                        checked: settingsController.alwaysActivateOnDrag
                        onToggled: settingsController.alwaysActivateOnDrag = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, the zone overlay appears on every window drag without requiring a modifier key or mouse button.")
                    }

                    ModifierAndMouseCheckBoxes {
                        id: dragActivationInput

                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Hold to activate:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.6
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: settingsController.dragActivationTriggers
                        defaultTriggers: settingsController.defaultDragActivationTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold modifier or use mouse button to show zones while dragging. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            settingsController.dragActivationTriggers = triggers;
                        }
                    }

                    CheckBox {
                        Layout.fillWidth: true
                        Kirigami.FormData.label: i18n("Toggle mode:")
                        enabled: !alwaysActivateCheck.checked
                        opacity: enabled ? 1 : 0.6
                        text: i18n("Tap trigger to toggle overlay")
                        checked: appSettings.toggleActivation
                        onToggled: appSettings.toggleActivation = checked
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
                        checked: appSettings.zoneSpanEnabled
                        onToggled: appSettings.zoneSpanEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, you can paint across multiple zones to snap a window to the combined area.")
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Modifier:")
                        enabled: zoneSpanEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: settingsController.zoneSpanTriggers
                        defaultTriggers: settingsController.defaultZoneSpanTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold modifier or use mouse button while dragging to paint across zones. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            settingsController.zoneSpanTriggers = triggers;
                        }
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
                            value: appSettings.adjacentThreshold
                            onValueModified: appSettings.adjacentThreshold = value
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
                        checked: appSettings.snapAssistFeatureEnabled
                        onToggled: appSettings.snapAssistFeatureEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Show a window picker after snapping to fill remaining empty zones")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Behavior:")
                        text: i18n("Always show after snapping")
                        checked: appSettings.snapAssistEnabled
                        onToggled: appSettings.snapAssistEnabled = checked
                        enabled: snapAssistFeatureEnabledCheck.checked
                        opacity: enabled ? 1 : 0.6
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("When enabled, a window picker appears after every snap. When disabled, hold the trigger below while dropping to show the picker for that snap only.")
                    }

                    ModifierAndMouseCheckBoxes {
                        Layout.fillWidth: true
                        Layout.preferredWidth: root.sliderPreferredWidth
                        Kirigami.FormData.label: i18n("Hold to enable:")
                        enabled: snapAssistFeatureEnabledCheck.checked && !appSettings.snapAssistEnabled
                        opacity: enabled ? 1 : 0.6
                        allowMultiple: true
                        acceptMode: acceptModeAll
                        triggers: settingsController.snapAssistTriggers
                        defaultTriggers: settingsController.defaultSnapAssistTriggers
                        tooltipEnabled: true
                        customTooltipText: i18n("Hold this modifier or mouse button when releasing a window to show the picker for that snap only. Add multiple triggers to activate with any of them.")
                        onTriggersModified: (triggers) => {
                            settingsController.snapAssistTriggers = triggers;
                        }
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
                enabled: appSettings.snappingEnabled

                header: Kirigami.Heading {
                    text: i18n("Behavior")
                    level: 3
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        Kirigami.FormData.label: i18n("Display:")
                        text: i18n("Show zones on all monitors while dragging")
                        checked: appSettings.showZonesOnAllMonitors
                        onToggled: appSettings.showZonesOnAllMonitors = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Resolution:")
                        text: i18n("Re-snap windows to their zones after resolution changes")
                        checked: appSettings.keepWindowsInZonesOnResolutionChange
                        onToggled: appSettings.keepWindowsInZonesOnResolutionChange = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("New windows:")
                        text: i18n("Move new windows to their last used zone")
                        checked: appSettings.moveNewWindowsToLastZone
                        onToggled: appSettings.moveNewWindowsToLastZone = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Unsnapping:")
                        text: i18n("Restore original window size when unsnapping")
                        checked: appSettings.restoreOriginalSizeOnUnsnap
                        onToggled: appSettings.restoreOriginalSizeOnUnsnap = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Reopening:")
                        text: i18n("Restore windows to their previous zone")
                        checked: appSettings.restoreWindowsToZonesOnLogin
                        onToggled: appSettings.restoreWindowsToZonesOnLogin = checked
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
                        currentIndex: Math.max(0, indexOfValue(appSettings.stickyWindowHandling))
                        onActivated: appSettings.stickyWindowHandling = currentValue
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Sticky windows appear on all desktops. Choose how snapping should behave.")
                    }

                }

            }

        }

        // =====================================================================
        // PER-MONITOR SNAPPING OVERRIDES
        // =====================================================================
        MonitorSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController
            featureEnabled: settingsController.settings.snappingEnabled
            selectedScreenName: snappingHelper.selectedScreenName
            hasOverrides: snappingHelper.hasOverrides
            onSelectedScreenNameChanged: snappingHelper.selectedScreenName = selectedScreenName
            onResetClicked: snappingHelper.clearOverrides()
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
                enabled: appSettings.snappingEnabled

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
                            value: root.snappingSettingValue("ZonePadding", appSettings.zonePadding)
                            onValueModified: root.writeSnappingSetting("ZonePadding", value, function(v) {
                                appSettings.zonePadding = v;
                            })
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
                            value: root.snappingSettingValue("OuterGap", appSettings.outerGap)
                            enabled: !perSideCheck.checked
                            onValueModified: root.writeSnappingSetting("OuterGap", value, function(v) {
                                appSettings.outerGap = v;
                            })
                            Accessible.name: i18n("Edge gap")
                        }

                        Label {
                            text: i18n("px")
                            visible: !perSideCheck.checked
                        }

                        CheckBox {
                            id: perSideCheck

                            text: i18n("Set per side")
                            checked: root.snappingSettingValue("UsePerSideOuterGap", appSettings.usePerSideOuterGap)
                            onToggled: root.writeSnappingSetting("UsePerSideOuterGap", checked, function(v) {
                                appSettings.usePerSideOuterGap = v;
                            })
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
                            value: root.snappingSettingValue("OuterGapTop", appSettings.outerGapTop)
                            onValueModified: root.writeSnappingSetting("OuterGapTop", value, function(v) {
                                appSettings.outerGapTop = v;
                            })
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
                            value: root.snappingSettingValue("OuterGapBottom", appSettings.outerGapBottom)
                            onValueModified: root.writeSnappingSetting("OuterGapBottom", value, function(v) {
                                appSettings.outerGapBottom = v;
                            })
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
                            value: root.snappingSettingValue("OuterGapLeft", appSettings.outerGapLeft)
                            onValueModified: root.writeSnappingSetting("OuterGapLeft", value, function(v) {
                                appSettings.outerGapLeft = v;
                            })
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
                            value: root.snappingSettingValue("OuterGapRight", appSettings.outerGapRight)
                            onValueModified: root.writeSnappingSetting("OuterGapRight", value, function(v) {
                                appSettings.outerGapRight = v;
                            })
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
        // ZONE SELECTOR (per-monitor popup configuration)
        // =====================================================================
        ZoneSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController.settings
            controller: settingsController
            constants: root
            isCurrentTab: true
            screenAspectRatio: root.screenAspectRatio
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
        nameFilters: ["JSON files (*.json)", "All files (*)"]
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
