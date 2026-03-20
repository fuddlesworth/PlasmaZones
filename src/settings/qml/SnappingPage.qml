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

        // =====================================================================
        // ACTIVATION
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true

            header: Kirigami.Heading {
                text: i18n("Activation")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    Layout.fillWidth: true

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
                    }

                }

                CheckBox {
                    text: i18n("Tap trigger to toggle overlay")
                    checked: kcm.toggleActivation
                    onToggled: kcm.toggleActivation = checked
                    enabled: kcm.snappingEnabled
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Label {
                    text: i18n("Zone Span")
                    font.bold: true
                }

                CheckBox {
                    text: i18n("Enable zone spanning")
                    checked: kcm.zoneSpanEnabled
                    onToggled: kcm.zoneSpanEnabled = checked
                    enabled: kcm.snappingEnabled
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: kcm.snappingEnabled && kcm.zoneSpanEnabled

                    Label {
                        text: i18n("Edge threshold:")
                    }

                    SpinBox {
                        from: 5
                        to: root.thresholdMax
                        value: kcm.adjacentThreshold
                        onValueModified: kcm.adjacentThreshold = value
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                Label {
                    text: i18n("Snap Assist")
                    font.bold: true
                }

                CheckBox {
                    id: snapAssistFeatureCheck

                    text: i18n("Enable snap assist window picker")
                    checked: kcm.snapAssistFeatureEnabled
                    onToggled: kcm.snapAssistFeatureEnabled = checked
                    enabled: kcm.snappingEnabled
                }

                CheckBox {
                    text: i18n("Always show after snapping")
                    checked: kcm.snapAssistEnabled
                    onToggled: kcm.snapAssistEnabled = checked
                    enabled: kcm.snappingEnabled && snapAssistFeatureCheck.checked
                }

            }

        }

        // =====================================================================
        // DISPLAY
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.snappingEnabled

            header: Kirigami.Heading {
                text: i18n("Display")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Show zones on all monitors while dragging")
                    checked: kcm.showZonesOnAllMonitors
                    onToggled: kcm.showZonesOnAllMonitors = checked
                }

                CheckBox {
                    text: i18n("Show zone numbers")
                    checked: kcm.showZoneNumbers
                    onToggled: kcm.showZoneNumbers = checked
                }

                CheckBox {
                    text: i18n("Flash zones when switching layouts")
                    checked: kcm.flashZonesOnSwitch
                    onToggled: kcm.flashZonesOnSwitch = checked
                }

            }

        }

        // =====================================================================
        // APPEARANCE
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.snappingEnabled

            header: Kirigami.Heading {
                text: i18n("Appearance")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // --- Colors ---
                Label {
                    text: i18n("Colors")
                    font.bold: true
                }

                CheckBox {
                    id: useSystemColorsCheck

                    text: i18n("Use system accent color")
                    checked: kcm.useSystemColors
                    onToggled: kcm.useSystemColors = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: !useSystemColorsCheck.checked

                    Label {
                        text: i18n("Highlight:")
                    }

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
                    spacing: Kirigami.Units.smallSpacing
                    visible: !useSystemColorsCheck.checked

                    Label {
                        text: i18n("Inactive:")
                    }

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
                    spacing: Kirigami.Units.smallSpacing
                    visible: !useSystemColorsCheck.checked

                    Label {
                        text: i18n("Border:")
                    }

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
                    Layout.fillWidth: true
                }

                // --- Opacity ---
                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Active opacity:")
                    }

                    Slider {
                        id: activeOpacitySlider

                        Layout.preferredWidth: root.sliderPreferredWidth
                        from: 0
                        to: root.opacitySliderMax
                        value: kcm.activeOpacity * root.opacitySliderMax
                        onMoved: kcm.activeOpacity = value / root.opacitySliderMax
                    }

                    Label {
                        text: Math.round(activeOpacitySlider.value) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Inactive opacity:")
                    }

                    Slider {
                        id: inactiveOpacitySlider

                        Layout.preferredWidth: root.sliderPreferredWidth
                        from: 0
                        to: root.opacitySliderMax
                        value: kcm.inactiveOpacity * root.opacitySliderMax
                        onMoved: kcm.inactiveOpacity = value / root.opacitySliderMax
                    }

                    Label {
                        text: Math.round(inactiveOpacitySlider.value) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // --- Border ---
                Label {
                    text: i18n("Border")
                    font.bold: true
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Border width:")
                    }

                    SpinBox {
                        from: 0
                        to: root.borderWidthMax
                        value: kcm.borderWidth
                        onValueModified: kcm.borderWidth = value
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Border radius:")
                    }

                    SpinBox {
                        from: 0
                        to: root.borderRadiusMax
                        value: kcm.borderRadius
                        onValueModified: kcm.borderRadius = value
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                CheckBox {
                    text: i18n("Enable blur behind zones")
                    checked: kcm.enableBlur
                    onToggled: kcm.enableBlur = checked
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // --- Zone Labels ---
                Label {
                    text: i18n("Zone Labels")
                    font.bold: true
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: !useSystemColorsCheck.checked

                    Label {
                        text: i18n("Label color:")
                    }

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
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Font family:")
                    }

                    TextField {
                        Layout.preferredWidth: root.sliderPreferredWidth
                        text: kcm.labelFontFamily
                        placeholderText: i18n("System default")
                        onEditingFinished: kcm.labelFontFamily = text
                    }

                    Button {
                        icon.name: "edit-clear"
                        visible: kcm.labelFontFamily !== ""
                        ToolTip.text: i18n("Reset to system default")
                        ToolTip.visible: hovered
                        onClicked: kcm.labelFontFamily = ""
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Font weight:")
                    }

                    ComboBox {
                        id: fontWeightCombo

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

                }

                RowLayout {
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
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Font scale:")
                    }

                    Slider {
                        id: fontSizeScaleSlider

                        Layout.preferredWidth: root.sliderPreferredWidth
                        from: 25
                        to: 300
                        stepSize: 5
                        value: kcm.labelFontSizeScale * 100
                        onMoved: kcm.labelFontSizeScale = value / 100
                    }

                    Label {
                        text: Math.round(fontSizeScaleSlider.value) + "%"
                        Layout.preferredWidth: root.sliderValueLabelWidth
                    }

                }

                Button {
                    text: i18n("Reset Font to Defaults")
                    icon.name: "edit-clear"
                    visible: kcm.labelFontFamily !== "" || kcm.labelFontWeight !== Font.Bold || kcm.labelFontItalic || kcm.labelFontUnderline || kcm.labelFontStrikeout || Math.abs(kcm.labelFontSizeScale - 1) > 0.01
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

        }

        // =====================================================================
        // SHADER EFFECTS
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.snappingEnabled

            header: Kirigami.Heading {
                text: i18n("Shader Effects")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    id: shaderEffectsCheck

                    text: i18n("Enable shader effects")
                    checked: kcm.enableShaderEffects
                    onToggled: kcm.enableShaderEffects = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: shaderEffectsCheck.checked

                    Label {
                        text: i18n("Frame rate:")
                    }

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
                    Layout.fillWidth: true
                }

                Label {
                    text: i18n("Audio Visualization")
                    font.bold: true
                }

                CheckBox {
                    id: audioVizCheck

                    text: i18n("Enable CAVA audio spectrum")
                    enabled: shaderEffectsCheck.checked
                    checked: kcm.enableAudioVisualizer
                    onToggled: kcm.enableAudioVisualizer = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: shaderEffectsCheck.checked && audioVizCheck.checked

                    Label {
                        text: i18n("Spectrum bars:")
                    }

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

        // =====================================================================
        // ZONE GEOMETRY
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.snappingEnabled

            header: Kirigami.Heading {
                text: i18n("Zone Geometry")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Zone padding:")
                    }

                    SpinBox {
                        from: 0
                        to: root.paddingMax
                        value: kcm.zonePadding
                        onValueModified: kcm.zonePadding = value
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Edge gap:")
                    }

                    SpinBox {
                        from: 0
                        to: root.paddingMax
                        value: kcm.outerGap
                        onValueModified: kcm.outerGap = value
                        enabled: !perSideCheck.checked
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
                    columns: 6
                    visible: perSideCheck.checked
                    Layout.leftMargin: Kirigami.Units.largeSpacing
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
                    }

                    Label {
                        text: i18n("px")
                    }

                    Label {
                        text: i18n("Bottom:")
                    }

                    SpinBox {
                        from: 0
                        to: root.paddingMax
                        value: kcm.outerGapBottom
                        onValueModified: kcm.outerGapBottom = value
                    }

                    Label {
                        text: i18n("px")
                    }

                    Label {
                        text: i18n("Left:")
                    }

                    SpinBox {
                        from: 0
                        to: root.paddingMax
                        value: kcm.outerGapLeft
                        onValueModified: kcm.outerGapLeft = value
                    }

                    Label {
                        text: i18n("px")
                    }

                    Label {
                        text: i18n("Right:")
                    }

                    SpinBox {
                        from: 0
                        to: root.paddingMax
                        value: kcm.outerGapRight
                        onValueModified: kcm.outerGapRight = value
                    }

                    Label {
                        text: i18n("px")
                    }

                }

            }

        }

        // =====================================================================
        // BEHAVIOR
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.snappingEnabled

            header: Kirigami.Heading {
                text: i18n("Behavior")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Re-snap windows to their zones after resolution changes")
                    checked: kcm.keepWindowsInZonesOnResolutionChange
                    onToggled: kcm.keepWindowsInZonesOnResolutionChange = checked
                }

                CheckBox {
                    text: i18n("Move new windows to their last used zone")
                    checked: kcm.moveNewWindowsToLastZone
                    onToggled: kcm.moveNewWindowsToLastZone = checked
                }

                CheckBox {
                    text: i18n("Restore original window size when unsnapping")
                    checked: kcm.restoreOriginalSizeOnUnsnap
                    onToggled: kcm.restoreOriginalSizeOnUnsnap = checked
                }

                CheckBox {
                    text: i18n("Restore windows to their previous zone on login")
                    checked: kcm.restoreWindowsToZonesOnLogin
                    onToggled: kcm.restoreWindowsToZonesOnLogin = checked
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Sticky windows:")
                    }

                    ComboBox {
                        id: stickyHandlingCombo

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
                    }

                }

            }

        }

        // =====================================================================
        // ZONE SELECTOR
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: kcm.snappingEnabled

            header: Kirigami.Heading {
                text: i18n("Zone Selector")
                level: 2
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                CheckBox {
                    text: i18n("Enable zone selector popup")
                    checked: kcm.zoneSelectorEnabled
                    onToggled: kcm.zoneSelectorEnabled = checked
                    font.bold: true
                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // --- Position ---
                Label {
                    text: i18n("Position & Trigger")
                    font.bold: true
                    enabled: kcm.zoneSelectorEnabled
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Position:")
                    }

                    ComboBox {
                        id: zoneSelectorPositionCombo

                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Top-Left"),
                            "value": 0
                        }, {
                            "text": i18n("Top"),
                            "value": 1
                        }, {
                            "text": i18n("Top-Right"),
                            "value": 2
                        }, {
                            "text": i18n("Left"),
                            "value": 3
                        }, {
                            "text": i18n("Center"),
                            "value": 4
                        }, {
                            "text": i18n("Right"),
                            "value": 5
                        }, {
                            "text": i18n("Bottom-Left"),
                            "value": 6
                        }, {
                            "text": i18n("Bottom"),
                            "value": 7
                        }, {
                            "text": i18n("Bottom-Right"),
                            "value": 8
                        }]
                        currentIndex: Math.max(0, indexOfValue(kcm.zoneSelectorPosition))
                        onActivated: kcm.zoneSelectorPosition = currentValue
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Trigger distance:")
                    }

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
                    Layout.fillWidth: true
                }

                // --- Layout Arrangement ---
                Label {
                    text: i18n("Layout Arrangement")
                    font.bold: true
                    enabled: kcm.zoneSelectorEnabled
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Arrangement:")
                    }

                    ComboBox {
                        id: layoutModeCombo

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

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: kcm.zoneSelectorLayoutMode === 0
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Grid columns:")
                    }

                    SpinBox {
                        from: 1
                        to: root.zoneSelectorGridColumnsMax
                        value: kcm.zoneSelectorGridColumns
                        onValueModified: kcm.zoneSelectorGridColumns = value
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: kcm.zoneSelectorLayoutMode === 0
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Max visible rows:")
                    }

                    SpinBox {
                        from: 1
                        to: 10
                        value: kcm.zoneSelectorMaxRows
                        onValueModified: kcm.zoneSelectorMaxRows = value
                    }

                }

                Kirigami.Separator {
                    Layout.fillWidth: true
                }

                // --- Preview Size ---
                Label {
                    text: i18n("Preview Size")
                    font.bold: true
                    enabled: kcm.zoneSelectorEnabled
                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Size mode:")
                    }

                    ComboBox {
                        id: sizeModeCombo

                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Auto"),
                            "value": 0
                        }, {
                            "text": i18n("Manual"),
                            "value": 1
                        }]
                        currentIndex: Math.max(0, indexOfValue(kcm.zoneSelectorSizeMode))
                        onActivated: kcm.zoneSelectorSizeMode = currentValue
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: kcm.zoneSelectorSizeMode === 1
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Preview width:")
                    }

                    SpinBox {
                        from: root.zoneSelectorPreviewWidthMin
                        to: root.zoneSelectorPreviewWidthMax
                        value: kcm.zoneSelectorPreviewWidth
                        onValueModified: {
                            kcm.zoneSelectorPreviewWidth = value;
                            if (kcm.zoneSelectorPreviewLockAspect) {
                                var newHeight = Math.round(value / root.screenAspectRatio);
                                newHeight = Math.max(root.zoneSelectorPreviewHeightMin, Math.min(root.zoneSelectorPreviewHeightMax, newHeight));
                                kcm.zoneSelectorPreviewHeight = newHeight;
                            }
                        }
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing
                    visible: kcm.zoneSelectorSizeMode === 1
                    enabled: kcm.zoneSelectorEnabled

                    Label {
                        text: i18n("Preview height:")
                    }

                    SpinBox {
                        from: root.zoneSelectorPreviewHeightMin
                        to: root.zoneSelectorPreviewHeightMax
                        value: kcm.zoneSelectorPreviewHeight
                        onValueModified: kcm.zoneSelectorPreviewHeight = value
                        enabled: !kcm.zoneSelectorPreviewLockAspect
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                CheckBox {
                    text: i18n("Lock aspect ratio")
                    checked: kcm.zoneSelectorPreviewLockAspect
                    onToggled: kcm.zoneSelectorPreviewLockAspect = checked
                    visible: kcm.zoneSelectorSizeMode === 1
                    enabled: kcm.zoneSelectorEnabled
                }

                // Live preview
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: previewRect.height + 30
                    visible: kcm.zoneSelectorEnabled
                    enabled: kcm.zoneSelectorEnabled

                    Rectangle {
                        id: previewRect

                        anchors.horizontalCenter: parent.horizontalCenter
                        width: kcm.zoneSelectorSizeMode === 0 ? Math.round(180 * (root.screenAspectRatio / (16 / 9))) : kcm.zoneSelectorPreviewWidth
                        height: kcm.zoneSelectorSizeMode === 0 ? Math.round(width / root.screenAspectRatio) : kcm.zoneSelectorPreviewHeight
                        color: "transparent"
                        radius: Kirigami.Units.smallSpacing
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
                        anchors.top: previewRect.bottom
                        anchors.topMargin: Kirigami.Units.smallSpacing
                        text: previewRect.width + " x " + previewRect.height + " px"
                        font: Kirigami.Theme.fixedWidthFont
                        opacity: 0.7
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
