// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Properties panel component for zone editing
 *
 * Displays and allows editing of zone properties when a zone is selected.
 * Includes name, number, appearance settings, and delete action.
 */
Rectangle {
    id: propertyPanel

    required property var editorController
    required property string selectedZoneId
    required property var selectedZone
    required property int selectionCount
    required property bool hasMultipleSelection
    // Panel mode: "hidden" (no selection), "single" (one zone), "multiple" (many zones)
    readonly property string panelMode: {
        if (selectionCount === 0)
            return "hidden";

        if (selectionCount === 1)
            return "single";

        return "multiple";
    }
    // Multi-select color preview properties (stored at panel level to avoid context issues)
    // Use Kirigami.Theme colors with alpha for theme compatibility
    property color multiHighlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5)
    property color multiInactiveColor: Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25)
    property color multiBorderColor: Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8)
    // Computed property: true if ALL selected zones have useCustomColors enabled
    // Uses C++ method for O(n) lookup instead of O(n*m) JavaScript nested loops (performance optimization)
    readonly property bool allSelectedUseCustomColors: {
        if (!editorController || selectionCount < 2)
            return false;
        
        // Use zonesVersion for dependency tracking instead of accessing zones.
        // This avoids copying the entire QVariantList just to create a binding dependency.
        var _ = editorController.zonesVersion;
        
        // Use efficient C++ method for the actual check
        return editorController.allSelectedUseCustomColors();
    }

    // In RowLayout, use Layout properties to control width based on visibility
    Layout.preferredWidth: visible ? 280 : 0
    Layout.maximumWidth: visible ? 280 : 0
    Layout.minimumWidth: 0
    Layout.fillHeight: true
    color: Kirigami.Theme.backgroundColor
    // Check visibility based on panelMode - show for both single and multiple selections
    visible: panelMode !== "hidden"
    opacity: visible ? 1 : 0
    z: 50
    // Reset multi-select colors to defaults when entering multi-select mode
    onPanelModeChanged: {
        if (panelMode === "multiple") {
            // Reset to theme defaults
            multiHighlightColor = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);
            multiInactiveColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25);
            multiBorderColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8);
        }
    }

    // Constants for appearance defaults (accessible to all components)
    QtObject {
        id: appearanceConstants

        readonly property real defaultOpacity: 0.5 // From Defaults::Opacity in constants.h
        readonly property real defaultInactiveOpacity: 0.3 // From Defaults::InactiveOpacity in constants.h
        readonly property int defaultBorderWidth: 2 // From Defaults::BorderWidth
        readonly property int defaultBorderRadius: 8 // From Defaults::BorderRadius
        readonly property int colorButtonSize: Kirigami.Units.gridUnit * 3 // 24px
        readonly property string transparentArgbHex: "#00000000" // Transparent black in ARGB format (alpha=0)

        // Helper function to parse ARGB hex strings (from QColor::HexArgb format)
        // QML's Qt.color() expects RGBA format, but we store ARGB
        function parseArgbHex(hexString) {
            if (!hexString || typeof hexString !== 'string')
                return Qt.transparent;

            // Remove # if present
            var hex = hexString.replace('#', '');
            // ARGB format: AARRGGBB (8 chars) or ARGB (4 chars)
            if (hex.length === 8) {
                // Parse ARGB: AARRGGBB
                var a = parseInt(hex.substring(0, 2), 16) / 255;
                var r = parseInt(hex.substring(2, 4), 16) / 255;
                var g = parseInt(hex.substring(4, 6), 16) / 255;
                var b = parseInt(hex.substring(6, 8), 16) / 255;
                return Qt.rgba(r, g, b, a);
            } else if (hex.length === 4) {
                // Parse ARGB shorthand: ARGB
                var a = parseInt(hex.substring(0, 1), 16) / 15;
                var r = parseInt(hex.substring(1, 2), 16) / 15;
                var g = parseInt(hex.substring(2, 3), 16) / 15;
                var b = parseInt(hex.substring(3, 4), 16) / 15;
                return Qt.rgba(r, g, b, a);
            } else {
                // Try Qt.color() as fallback (might be RGB format)
                return Qt.color(hexString);
            }
        }

        // Helper function to convert QColor to ARGB hex string (#AARRGGBB)
        // QColor.toString() returns #RRGGBB without alpha, so we need to construct ARGB manually
        function colorToArgbHex(colorValue) {
            if (!colorValue)
                return appearanceConstants.transparentArgbHex;

            // Convert color components to 0-255 range
            var a = Math.round(colorValue.a * 255);
            var r = Math.round(colorValue.r * 255);
            var g = Math.round(colorValue.g * 255);
            var b = Math.round(colorValue.b * 255);
            // Format as ARGB hex string: #AARRGGBB
            var aHex = a.toString(16).padStart(2, '0').toUpperCase();
            var rHex = r.toString(16).padStart(2, '0').toUpperCase();
            var gHex = g.toString(16).padStart(2, '0').toUpperCase();
            var bHex = b.toString(16).padStart(2, '0').toUpperCase();
            return "#" + aHex + rHex + gHex + bHex;
        }

    }

    ColumnLayout {
        // ═══════════════════════════════════════════════════════════════
        // COLOR DIALOGS
        // ═══════════════════════════════════════════════════════════════
        // ═══════════════════════════════════════════════════════════════
        // MULTI-SELECT COLOR DIALOGS
        // ═══════════════════════════════════════════════════════════════
        // ═══════════════════════════════════════════════════════════════
        // COLORBUTTON COMPONENT (inline, same as KCM implementation)
        // ═══════════════════════════════════════════════════════════════

        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing // Use theme spacing (16px)
        visible: parent.visible
        spacing: Kirigami.Units.gridUnit // Use theme spacing (8px)

        // Header row with title and close button
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.gridUnit // Use theme spacing

            Label {
                Layout.fillWidth: true
                text: panelMode === "single" ? i18nc("@title", "Zone Properties") : i18ncp("@title", "1 Zone Selected", "%1 Zones Selected", selectionCount)
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.1
            }

            ToolButton {
                id: closeButton

                icon.name: "window-close"
                onClicked: {
                    if (editorController)
                        editorController.clearSelection();

                }
                ToolTip.text: i18nc("@tooltip", "Close panel")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@info:accessibility", "Close properties panel")
                Accessible.description: ToolTip.text
                Accessible.role: Accessible.Button
            }

        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            Kirigami.FormLayout {
                // ═══════════════════════════════════════════════════════════════
                // ZONE PROPERTIES (shown for single zone selection only)
                // ═══════════════════════════════════════════════════════════════

                width: parent.width
                // Explicitly set wideMode to prevent binding loop
                wideMode: width > Kirigami.Units.gridUnit * 20

                // ═══════════════════════════════════════════════════════════════
                // MULTI-SELECT APPEARANCE SECTION
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "multiple"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Appearance (All Selected)")
                }

                // Use custom colors toggle (multi-select)
                CheckBox {
                    id: multiUseCustomColorsCheck

                    visible: panelMode === "multiple"
                    Kirigami.FormData.label: i18nc("@label", "Custom colors:")
                    text: i18nc("@option:check", "Enable for all selected")
                    checked: propertyPanel.allSelectedUseCustomColors
                    enabled: editorController !== null
                    Accessible.name: i18nc("@info:accessibility", "Enable custom colors for all selected zones")
                    Accessible.description: i18nc("@info", "Enable custom color settings for all selected zones")
                    ToolTip.text: i18nc("@info:tooltip", "Enable custom colors for all selected zones")
                    onToggled: {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("useCustomColors", checked);

                    }
                }

                // Highlight color picker (multi-select)
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Highlight:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        id: multiHighlightColorPreview

                        width: appearanceConstants.colorButtonSize
                        height: appearanceConstants.colorButtonSize
                        radius: Kirigami.Units.smallSpacing
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        // Preview shows color with opacity applied (matches actual zone appearance)
                        color: Qt.rgba(propertyPanel.multiHighlightColor.r, propertyPanel.multiHighlightColor.g, propertyPanel.multiHighlightColor.b, propertyPanel.multiHighlightColor.a * (multiActiveOpacitySlider.value / 100))
                        Accessible.name: i18nc("@label", "Highlight color picker for all selected zones")
                        ToolTip.text: i18nc("@info:tooltip", "Choose highlight color for all selected zones")

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                multiHighlightColorDialog.selectedColor = propertyPanel.multiHighlightColor;
                                multiHighlightColorDialog.open();
                            }
                        }

                    }

                    Label {
                        text: i18nc("@info", "Click to set for all")
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    }

                }

                // Inactive color picker (multi-select)
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Inactive:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        id: multiInactiveColorPreview

                        width: appearanceConstants.colorButtonSize
                        height: appearanceConstants.colorButtonSize
                        radius: Kirigami.Units.smallSpacing
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        // Preview shows color with opacity applied (matches actual zone appearance)
                        color: Qt.rgba(propertyPanel.multiInactiveColor.r, propertyPanel.multiInactiveColor.g, propertyPanel.multiInactiveColor.b, propertyPanel.multiInactiveColor.a * (multiInactiveOpacitySlider.value / 100))
                        Accessible.name: i18nc("@label", "Inactive color picker for all selected zones")
                        ToolTip.text: i18nc("@info:tooltip", "Choose inactive color for all selected zones")

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                multiInactiveColorDialog.selectedColor = propertyPanel.multiInactiveColor;
                                multiInactiveColorDialog.open();
                            }
                        }

                    }

                    Label {
                        text: i18nc("@info", "Click to set for all")
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    }

                }

                // Border color picker (multi-select)
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Border:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Rectangle {
                        id: multiBorderColorPreview

                        width: appearanceConstants.colorButtonSize
                        height: appearanceConstants.colorButtonSize
                        radius: Kirigami.Units.smallSpacing
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        color: propertyPanel.multiBorderColor
                        Accessible.name: i18nc("@label", "Border color picker for all selected zones")
                        ToolTip.text: i18nc("@info:tooltip", "Choose border color for all selected zones")

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                multiBorderColorDialog.selectedColor = propertyPanel.multiBorderColor;
                                multiBorderColorDialog.open();
                            }
                        }

                    }

                    Label {
                        text: i18nc("@info", "Click to set for all")
                        color: Kirigami.Theme.disabledTextColor
                        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    }

                }

                // Active opacity slider (multi-select)
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Active opacity:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: multiActiveOpacitySlider

                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: appearanceConstants.defaultOpacity * 100
                        enabled: editorController !== null
                        Accessible.name: i18nc("@label", "Active opacity for all selected zones")
                        ToolTip.text: i18nc("@info:tooltip", "Set active opacity for all selected zones (0-100%)")
                        onMoved: {
                            Qt.callLater(function() {
                                if (editorController) {
                                    var opacityValue = multiActiveOpacitySlider.value / 100;
                                    editorController.updateSelectedZonesAppearance("activeOpacity", opacityValue);
                                }
                            });
                        }
                    }

                    Label {
                        text: Math.round(multiActiveOpacitySlider.value) + "%"
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Inactive opacity slider (multi-select)
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Inactive opacity:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: multiInactiveOpacitySlider

                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        value: appearanceConstants.defaultInactiveOpacity * 100
                        enabled: editorController !== null
                        Accessible.name: i18nc("@label", "Inactive opacity for all selected zones")
                        ToolTip.text: i18nc("@info:tooltip", "Set inactive opacity for all selected zones (0-100%)")
                        onMoved: {
                            Qt.callLater(function() {
                                if (editorController) {
                                    var opacityValue = multiInactiveOpacitySlider.value / 100;
                                    editorController.updateSelectedZonesAppearance("inactiveOpacity", opacityValue);
                                }
                            });
                        }
                    }

                    Label {
                        text: Math.round(multiInactiveOpacitySlider.value) + "%"
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Border width spinbox (multi-select)
                SpinBox {
                    id: multiBorderWidthSpinBox

                    Kirigami.FormData.label: i18nc("@label", "Border width:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    from: 0
                    to: 20
                    value: appearanceConstants.defaultBorderWidth
                    enabled: editorController !== null
                    Accessible.name: i18nc("@label", "Border width for all selected zones")
                    ToolTip.text: i18nc("@info:tooltip", "Set border width for all selected zones (0-20)")
                    onValueModified: {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("borderWidth", value);

                    }
                }

                // Border radius spinbox (multi-select)
                SpinBox {
                    id: multiBorderRadiusSpinBox

                    Kirigami.FormData.label: i18nc("@label", "Border radius:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    from: 0
                    to: 50
                    value: appearanceConstants.defaultBorderRadius
                    enabled: editorController !== null
                    Accessible.name: i18nc("@label", "Border radius for all selected zones")
                    ToolTip.text: i18nc("@info:tooltip", "Set corner radius for all selected zones (0-50)")
                    onValueModified: {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("borderRadius", value);

                    }
                }

                // ═══════════════════════════════════════════════════════════════
                // MULTI-SELECT ACTIONS SECTION
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "multiple"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Actions")
                }

                Label {
                    visible: panelMode === "multiple"
                    Layout.fillWidth: true
                    text: i18nc("@info", "Actions will apply to all selected zones.")
                    wrapMode: Text.WordWrap
                    color: Kirigami.Theme.disabledTextColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                }

                Button {
                    visible: panelMode === "multiple"
                    Layout.fillWidth: true
                    text: i18nc("@action:button", "Delete Selected Zones")
                    icon.name: "edit-delete"
                    enabled: editorController !== null
                    Accessible.name: text
                    Accessible.description: i18ncp("@info", "Delete 1 selected zone", "Delete %1 selected zones", selectionCount)
                    onClicked: {
                        if (editorController)
                            editorController.deleteSelectedZones();

                    }
                }

                // Zone name
                TextField {
                    id: zoneNameField

                    // Validation state
                    property string validationError: ""
                    property bool hasError: validationError !== ""
                    // Debounce timer to avoid too many updates while typing
                    property Timer updateTimer

                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Name:")
                    text: selectedZone ? (selectedZone.name || "") : ""
                    enabled: editorController !== null && editorController !== undefined
                    Accessible.name: i18nc("@label", "Zone name")
                    Accessible.description: i18nc("@info", "Enter name for the zone")
                    onTextChanged: {
                        // Restart timer on each text change (debouncing)
                        if (selectedZoneId && editorController)
                            updateTimer.restart();

                    }
                    onEditingFinished: {
                        // Don't update if invalid

                        // Stop timer and update immediately when editing finishes
                        updateTimer.stop();
                        zoneNameField.validationError = ""; // Clear previous error
                        if (selectedZoneId && editorController) {
                            // Validate before updating
                            var error = editorController.validateZoneName(selectedZoneId, text);
                            if (error !== "") {
                                zoneNameField.validationError = error;
                                return ;
                            }
                            // Only update if text is different from current zone name
                            var currentZone = selectedZone;
                            if (currentZone && currentZone.name !== text)
                                editorController.updateZoneName(selectedZoneId, text);

                        }
                    }

                    updateTimer: Timer {
                        interval: 300 // 300ms delay
                        onTriggered: {
                            // Don't update if invalid

                            if (selectedZoneId && editorController) {
                                zoneNameField.validationError = ""; // Clear previous error
                                // Validate before updating
                                var error = editorController.validateZoneName(selectedZoneId, zoneNameField.text);
                                if (error !== "") {
                                    zoneNameField.validationError = error;
                                    return ;
                                }
                                // Only update if text is different from current zone name
                                var currentZone = selectedZone;
                                if (currentZone && currentZone.name !== zoneNameField.text)
                                    editorController.updateZoneName(selectedZoneId, zoneNameField.text);

                            }
                        }
                    }

                    // Only override background for error state - let Qt handle default styling
                    background: Rectangle {
                        color: zoneNameField.hasError ? Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.15) : zoneNameField.palette.base
                        radius: Kirigami.Units.smallSpacing
                        border.color: zoneNameField.hasError ? Kirigami.Theme.negativeTextColor : zoneNameField.palette.shadow
                        border.width: zoneNameField.hasError ? 2 : 1

                        Behavior on border.color {
                            ColorAnimation {
                                duration: 100
                            }

                        }

                        Behavior on border.width {
                            NumberAnimation {
                                duration: 100
                            }

                        }

                    }

                }

                // Zone name validation error
                Label {
                    Layout.fillWidth: true
                    text: zoneNameField.validationError
                    visible: panelMode === "single" && zoneNameField.hasError
                    color: Kirigami.Theme.negativeTextColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    wrapMode: Text.WordWrap
                    opacity: 0.9
                    Accessible.role: Accessible.AlertMessage
                }

                // Zone number - Use SpinBox for better UX and accessibility
                SpinBox {
                    // Don't update model if invalid

                    id: zoneNumberSpinBox

                    // Validation state
                    property string validationError: ""
                    property bool hasError: validationError !== ""

                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Number:")
                    from: 1
                    to: 99
                    value: selectedZone ? (selectedZone.zoneNumber || 1) : 1
                    enabled: editorController !== null && editorController !== undefined
                    Accessible.name: i18nc("@label", "Zone number")
                    Accessible.description: i18nc("@info", "Zone number (1-99)")
                    onValueModified: {
                        if (!selectedZoneId || !editorController)
                            return ;

                        validationError = ""; // Clear previous error
                        // Validate the new value (checks for collisions)
                        var error = editorController.validateZoneNumber(selectedZoneId, value);
                        if (error !== "") {
                            validationError = error;
                            // Show error but keep value - allow user to change to valid value
                            return ;
                        }
                        // Update is valid - proceed with update
                        editorController.updateZoneNumber(selectedZoneId, value);
                        validationError = ""; // Clear any previous errors
                    }
                }

                // Zone number validation error
                Label {
                    Layout.fillWidth: true
                    text: zoneNumberSpinBox.validationError
                    visible: panelMode === "single" && zoneNumberSpinBox.hasError
                    color: Kirigami.Theme.negativeTextColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    wrapMode: Text.WordWrap
                    opacity: 0.9
                    Accessible.role: Accessible.AlertMessage
                }

                // ═══════════════════════════════════════════════════════════════
                // APPEARANCE SECTION (single zone only)
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "single"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Appearance")
                }

                // Use custom colors toggle
                CheckBox {
                    id: useCustomColorsCheck

                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Custom colors:")
                    text: i18nc("@option:check", "Use custom colors")
                    // Explicit binding that tracks selectedZone changes
                    checked: (selectedZone !== null && selectedZone !== undefined && selectedZone.useCustomColors !== undefined) ? (selectedZone.useCustomColors === true) : false
                    enabled: selectedZone !== null && selectedZone !== undefined && editorController !== null && editorController !== undefined
                    Accessible.name: i18nc("@info:accessibility", "Enable custom colors for this zone")
                    Accessible.description: i18nc("@info", "Enable custom color settings for this zone")
                    ToolTip.text: i18nc("@info:tooltip", "Enable custom colors, opacity, and border settings")
                    onToggled: {
                        // Update zone property via EditorController
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "useCustomColors", checked);

                    }
                }

                // Highlight color picker
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Highlight:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    spacing: Kirigami.Units.smallSpacing

                    // Simple Rectangle for color preview - directly bound to zone color
                    Rectangle {
                        id: highlightColorPreview

                        // Store the base color (without opacity applied) for color picker
                        // Use selectedZone directly instead of looping through zones (performance optimization)
                        property color baseColor: {
                            if (!selectedZone)
                                return Qt.transparent;

                            var colorStr = selectedZone.highlightColor;
                            if (colorStr && typeof colorStr === 'string')
                                return appearanceConstants.parseArgbHex(colorStr);

                            return colorStr || Qt.transparent;
                        }

                        width: appearanceConstants.colorButtonSize
                        height: appearanceConstants.colorButtonSize
                        radius: Kirigami.Units.smallSpacing
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        // Preview shows color with opacity applied (matches actual zone appearance)
                        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, baseColor.a * (activeOpacitySlider.value / 100))
                        Accessible.name: i18nc("@label", "Highlight color picker")
                        Accessible.description: i18nc("@info", "Click to choose highlight color")
                        ToolTip.text: i18nc("@info:tooltip", "Choose color for highlighted/active zones")

                        // Checkerboard for transparency
                        Canvas {
                            anchors.fill: parent
                            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                            visible: highlightColorPreview.color.a < 1
                            onPaint: {
                                var ctx = getContext("2d");
                                var size = 4;
                                var lightGray = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
                                var white = Qt.rgba(1, 1, 1, 1);
                                for (var x = 0; x < width; x += size) {
                                    for (var y = 0; y < height; y += size) {
                                        ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white;
                                        ctx.fillRect(x, y, size, size);
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                            radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
                            color: highlightColorPreview.color
                        }

                        MouseArea {
                            // Fallback to theme highlight color

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Use baseColor (stored color without opacity) for color picker
                                var currentColor = highlightColorPreview.baseColor;
                                if (currentColor === Qt.transparent)
                                    currentColor = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);

                                highlightColorDialog.selectedColor = currentColor;
                                highlightColorDialog.open();
                            }
                        }

                    }

                    Label {
                        id: highlightColorLabel

                        // Use selectedZone directly instead of looping through zones (performance optimization)
                        readonly property color displayColor: {
                            if (!selectedZone)
                                return Qt.transparent;

                            var colorValue = selectedZone.highlightColor;
                            if (!colorValue)
                                return Qt.transparent;

                            if (typeof colorValue === 'string')
                                return appearanceConstants.parseArgbHex(colorValue);

                            return colorValue;
                        }

                        text: displayColor !== Qt.transparent ? displayColor.toString().toUpperCase() : ""
                        font: Kirigami.Theme.fixedWidthFont
                        Accessible.name: i18nc("@info:accessibility", "Highlight color hex code")
                    }

                }

                // Inactive color picker
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Inactive:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    spacing: Kirigami.Units.smallSpacing

                    // Simple Rectangle for color preview - directly bound to zone color
                    Rectangle {
                        id: inactiveColorPreview

                        // Store the base color (without opacity applied) for color picker
                        // Use selectedZone directly instead of looping through zones (performance optimization)
                        property color baseColor: {
                            if (!selectedZone)
                                return Qt.transparent;

                            var colorStr = selectedZone.inactiveColor;
                            if (colorStr && typeof colorStr === 'string')
                                return appearanceConstants.parseArgbHex(colorStr);

                            return colorStr || Qt.transparent;
                        }

                        width: appearanceConstants.colorButtonSize
                        height: appearanceConstants.colorButtonSize
                        radius: Kirigami.Units.smallSpacing
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        // Preview shows color with opacity applied (matches actual zone appearance)
                        color: Qt.rgba(baseColor.r, baseColor.g, baseColor.b, baseColor.a * (inactiveOpacitySlider.value / 100))
                        Accessible.name: i18nc("@label", "Inactive color picker")
                        Accessible.description: i18nc("@info", "Click to choose inactive zone color")
                        ToolTip.text: i18nc("@info:tooltip", "Choose color for non-selected zones")

                        // Checkerboard for transparency
                        Canvas {
                            anchors.fill: parent
                            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                            visible: inactiveColorPreview.color.a < 1
                            onPaint: {
                                var ctx = getContext("2d");
                                var size = 4;
                                var lightGray = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
                                var white = Qt.rgba(1, 1, 1, 1);
                                for (var x = 0; x < width; x += size) {
                                    for (var y = 0; y < height; y += size) {
                                        ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white;
                                        ctx.fillRect(x, y, size, size);
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                            radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
                            color: inactiveColorPreview.color
                        }

                        MouseArea {
                            // Fallback to theme disabledTextColor (matches multi-select defaults)

                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Use baseColor (stored color without opacity) for color picker
                                var currentColor = inactiveColorPreview.baseColor;
                                if (currentColor === Qt.transparent)
                                    currentColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25);

                                inactiveColorDialog.selectedColor = currentColor;
                                inactiveColorDialog.open();
                            }
                        }

                    }

                    Label {
                        id: inactiveColorLabel

                        // Use selectedZone directly instead of looping through zones (performance optimization)
                        readonly property color displayColor: {
                            if (!selectedZone)
                                return Qt.transparent;

                            var colorValue = selectedZone.inactiveColor;
                            if (!colorValue)
                                return Qt.transparent;

                            if (typeof colorValue === 'string')
                                return appearanceConstants.parseArgbHex(colorValue);

                            return colorValue;
                        }

                        text: displayColor !== Qt.transparent ? displayColor.toString().toUpperCase() : ""
                        font: Kirigami.Theme.fixedWidthFont
                        Accessible.name: i18nc("@info:accessibility", "Inactive color hex code")
                    }

                }

                // Border color picker
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Border:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    spacing: Kirigami.Units.smallSpacing

                    // Simple Rectangle for color preview - directly bound to zone color
                    Rectangle {
                        id: borderColorPreview

                        width: appearanceConstants.colorButtonSize
                        height: appearanceConstants.colorButtonSize
                        radius: Kirigami.Units.smallSpacing
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        // Use selectedZone directly instead of looping through zones (performance optimization)
                        color: {
                            if (!selectedZone)
                                return Qt.transparent;

                            var colorStr = selectedZone.borderColor;
                            if (colorStr && typeof colorStr === 'string')
                                return appearanceConstants.parseArgbHex(colorStr);

                            return colorStr || Qt.transparent;
                        }
                        Accessible.name: i18nc("@label", "Border color picker")
                        Accessible.description: i18nc("@info", "Click to choose border color")
                        ToolTip.text: i18nc("@info:tooltip", "Choose color for zone borders")

                        // Checkerboard for transparency
                        Canvas {
                            anchors.fill: parent
                            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                            visible: borderColorPreview.color.a < 1
                            onPaint: {
                                var ctx = getContext("2d");
                                var size = 4;
                                var lightGray = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
                                var white = Qt.rgba(1, 1, 1, 1);
                                for (var x = 0; x < width; x += size) {
                                    for (var y = 0; y < height; y += size) {
                                        ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white;
                                        ctx.fillRect(x, y, size, size);
                                    }
                                }
                            }
                        }

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                            radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
                            color: borderColorPreview.color
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                // Use theme disabledTextColor as fallback (matches multi-select defaults)
                                var currentColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8);
                                // Use selectedZone directly instead of looping through zones (performance optimization)
                                if (selectedZone && selectedZone.borderColor) {
                                    var colorValue = selectedZone.borderColor;
                                    currentColor = typeof colorValue === 'string' ? appearanceConstants.parseArgbHex(colorValue) : colorValue;
                                }
                                borderColorDialog.selectedColor = currentColor;
                                borderColorDialog.open();
                            }
                        }

                    }

                    Label {
                        id: borderColorLabel

                        // Use selectedZone directly instead of looping through zones (performance optimization)
                        readonly property color displayColor: {
                            if (!selectedZone)
                                return Qt.transparent;

                            var colorValue = selectedZone.borderColor;
                            if (!colorValue)
                                return Qt.transparent;

                            if (typeof colorValue === 'string')
                                return appearanceConstants.parseArgbHex(colorValue);

                            return colorValue;
                        }

                        text: displayColor !== Qt.transparent ? displayColor.toString().toUpperCase() : ""
                        font: Kirigami.Theme.fixedWidthFont
                        Accessible.name: i18nc("@info:accessibility", "Border color hex code")
                    }

                }

                // Active opacity slider - Use onMoved to avoid binding loops
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Active opacity:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: activeOpacitySlider

                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        // Simple binding - QML will track selectedZone.activeOpacity automatically
                        value: selectedZone && selectedZone.activeOpacity !== undefined ? (selectedZone.activeOpacity * 100) : (appearanceConstants.defaultOpacity * 100)
                        enabled: selectedZone !== null && selectedZone !== undefined && editorController !== null
                        Accessible.name: i18nc("@label", "Zone active opacity")
                        Accessible.description: i18nc("@info", "Active/highlighted opacity percentage (0-100%)")
                        ToolTip.text: i18nc("@info:tooltip", "Adjust zone opacity when highlighted (0-100%)")
                        // Only update when user drags slider, not when property changes programmatically
                        onMoved: {
                            // Use Qt.callLater to avoid binding context issues
                            Qt.callLater(function() {
                                if (selectedZoneId && editorController) {
                                    var opacityValue = activeOpacitySlider.value / 100;
                                    editorController.updateZoneAppearance(selectedZoneId, "activeOpacity", opacityValue);
                                }
                            });
                        }
                    }

                    Label {
                        text: Math.round(activeOpacitySlider.value) + "%"
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Inactive opacity slider
                RowLayout {
                    Kirigami.FormData.label: i18nc("@label", "Inactive opacity:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    spacing: Kirigami.Units.smallSpacing

                    Slider {
                        id: inactiveOpacitySlider

                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        stepSize: 1
                        // Simple binding - QML will track selectedZone.inactiveOpacity automatically
                        value: selectedZone && selectedZone.inactiveOpacity !== undefined ? (selectedZone.inactiveOpacity * 100) : (appearanceConstants.defaultInactiveOpacity * 100)
                        enabled: selectedZone !== null && selectedZone !== undefined && editorController !== null
                        Accessible.name: i18nc("@label", "Zone inactive opacity")
                        Accessible.description: i18nc("@info", "Inactive/non-highlighted opacity percentage (0-100%)")
                        ToolTip.text: i18nc("@info:tooltip", "Adjust zone opacity when not highlighted (0-100%)")
                        // Only update when user drags slider, not when property changes programmatically
                        onMoved: {
                            // Use Qt.callLater to avoid binding context issues
                            Qt.callLater(function() {
                                if (selectedZoneId && editorController) {
                                    var opacityValue = inactiveOpacitySlider.value / 100;
                                    editorController.updateZoneAppearance(selectedZoneId, "inactiveOpacity", opacityValue);
                                }
                            });
                        }
                    }

                    Label {
                        text: Math.round(inactiveOpacitySlider.value) + "%"
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Border width spinbox
                SpinBox {
                    id: borderWidthSpinBox

                    Kirigami.FormData.label: i18nc("@label", "Border width:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    from: 0
                    to: 20
                    // Simple binding - QML will track selectedZone.borderWidth automatically
                    value: selectedZone && selectedZone.borderWidth !== undefined ? selectedZone.borderWidth : appearanceConstants.defaultBorderWidth
                    enabled: selectedZone !== null && selectedZone !== undefined && editorController !== null
                    Accessible.name: i18nc("@label", "Border width in pixels")
                    Accessible.description: i18nc("@info", "Zone border width (0-20 pixels)")
                    ToolTip.text: i18nc("@info:tooltip", "Set zone border width in pixels (0-20)")
                    onValueModified: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "borderWidth", value);

                    }
                }

                // Border radius spinbox
                SpinBox {
                    id: borderRadiusSpinBox

                    Kirigami.FormData.label: i18nc("@label", "Border radius:")
                    visible: panelMode === "single" && selectedZone !== null && selectedZone !== undefined && useCustomColorsCheck.checked === true
                    from: 0
                    to: 50
                    // Simple binding - QML will track selectedZone.borderRadius automatically
                    value: selectedZone && selectedZone.borderRadius !== undefined ? selectedZone.borderRadius : appearanceConstants.defaultBorderRadius
                    enabled: selectedZone !== null && selectedZone !== undefined && editorController !== null
                    Accessible.name: i18nc("@label", "Border radius in pixels")
                    Accessible.description: i18nc("@info", "Zone corner radius (0-50 pixels)")
                    ToolTip.text: i18nc("@info:tooltip", "Set zone corner radius in pixels (0-50)")
                    onValueModified: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "borderRadius", value);

                    }
                }

                // ═══════════════════════════════════════════════════════════════
                // ACTIONS SECTION (single zone only)
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "single"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Actions")
                }

                // Delete button - placed in form layout, no spacer needed
                Button {
                    visible: panelMode === "single"
                    Layout.fillWidth: true
                    text: i18nc("@action:button", "Delete Zone")
                    icon.name: "edit-delete"
                    enabled: selectedZoneId !== "" && editorController !== null && editorController !== undefined
                    Accessible.name: text
                    Accessible.description: i18nc("@info", "Delete the selected zone")
                    onClicked: {
                        if (selectedZoneId && editorController)
                            editorController.deleteZone(selectedZoneId);

                    }
                }

                // Clear validation errors when zone selection changes and handle validation errors
                Connections {
                    // Color bindings will automatically update when selectedZone changes
                    // Color bindings will automatically update when zones list changes
                    // Color bindings will automatically update when zones list changes
                    // Don't revert value - allow user to continue editing and change to valid value
                    // The error is shown, but user can still modify the value

                    function onSelectedZoneIdChanged() {
                        // Stop any pending update timer when selection changes
                        zoneNameField.updateTimer.stop();
                        // Clear validation errors when selection changes
                        zoneNameField.validationError = "";
                        zoneNumberSpinBox.validationError = "";
                        // Update SpinBox value when zone selection changes
                        if (selectedZone)
                            zoneNumberSpinBox.value = selectedZone.zoneNumber || 1;

                    }

                    function onZoneColorChanged(zoneId) {
                    }

                    function onZonesChanged() {
                    }

                    function onZoneNameValidationError(zoneId, error) {
                        if (zoneId === selectedZoneId) {
                            zoneNameField.validationError = error;
                            // Revert to original value if validation fails
                            Qt.callLater(function() {
                                if (selectedZone)
                                    zoneNameField.text = selectedZone.name || "";

                            });
                        }
                    }

                    function onZoneNumberValidationError(zoneId, error) {
                        if (zoneId === selectedZoneId)
                            zoneNumberSpinBox.validationError = error;

                    }

                    target: editorController
                    enabled: (editorController !== null && editorController !== undefined) && (selectedZoneId !== "" && selectedZoneId !== null && selectedZoneId !== undefined)
                }

            }

        }

        ColorDialog {
            // Color binding will automatically update when zones list changes

            id: highlightColorDialog

            title: i18nc("@title:window", "Zone Highlight Color")
            onAccepted: {
                if (selectedZoneId && editorController) {
                    // Convert QColor to ARGB hex string format (#AARRGGBB)
                    // QColor.toString() returns #RRGGBB without alpha, so we must construct ARGB manually
                    var hexColor = appearanceConstants.colorToArgbHex(selectedColor);
                    editorController.updateZoneColor(selectedZoneId, "highlightColor", hexColor);
                }
            }

            // Update selectedColor when zone color changes
            Connections {
                function onZoneColorChanged(zoneId) {
                    if (zoneId === selectedZoneId && selectedZone && selectedZone.highlightColor && !highlightColorDialog.visible) {
                        var colorValue = selectedZone.highlightColor;
                        highlightColorDialog.selectedColor = typeof colorValue === 'string' ? appearanceConstants.parseArgbHex(colorValue) : colorValue;
                    }
                }

                target: editorController
                enabled: editorController !== null && selectedZoneId !== ""
            }

        }

        ColorDialog {
            // Color binding will automatically update when zones list changes

            id: inactiveColorDialog

            title: i18nc("@title:window", "Zone Inactive Color")
            onAccepted: {
                if (selectedZoneId && editorController) {
                    // Convert QColor to ARGB hex string format (#AARRGGBB)
                    // QColor.toString() returns #RRGGBB without alpha, so we must construct ARGB manually
                    var hexColor = appearanceConstants.colorToArgbHex(selectedColor);
                    editorController.updateZoneColor(selectedZoneId, "inactiveColor", hexColor);
                }
            }

            // Update selectedColor when zone color changes
            Connections {
                function onZoneColorChanged(zoneId) {
                    if (zoneId === selectedZoneId && selectedZone && selectedZone.inactiveColor && !inactiveColorDialog.visible) {
                        var colorValue = selectedZone.inactiveColor;
                        inactiveColorDialog.selectedColor = typeof colorValue === 'string' ? appearanceConstants.parseArgbHex(colorValue) : colorValue;
                    }
                }

                target: editorController
                enabled: editorController !== null && selectedZoneId !== ""
            }

        }

        ColorDialog {
            // Color binding will automatically update when zones list changes

            id: borderColorDialog

            title: i18nc("@title:window", "Zone Border Color")
            onAccepted: {
                if (selectedZoneId && editorController) {
                    // Convert QColor to ARGB hex string format (#AARRGGBB)
                    // QColor.toString() returns #RRGGBB without alpha, so we must construct ARGB manually
                    var hexColor = appearanceConstants.colorToArgbHex(selectedColor);
                    editorController.updateZoneColor(selectedZoneId, "borderColor", hexColor);
                }
            }

            // Update selectedColor when zone color changes
            Connections {
                function onZoneColorChanged(zoneId) {
                    if (zoneId === selectedZoneId && selectedZone && selectedZone.borderColor && !borderColorDialog.visible) {
                        var colorValue = selectedZone.borderColor;
                        borderColorDialog.selectedColor = typeof colorValue === 'string' ? appearanceConstants.parseArgbHex(colorValue) : colorValue;
                    }
                }

                target: editorController
                enabled: editorController !== null && selectedZoneId !== ""
            }

        }

        ColorDialog {
            id: multiHighlightColorDialog

            title: i18nc("@title:window", "Highlight Color for All Selected Zones")
            onAccepted: {
                if (editorController) {
                    // Convert QColor to ARGB hex string format (#AARRGGBB)
                    var hexColor = appearanceConstants.colorToArgbHex(selectedColor);
                    editorController.updateSelectedZonesColor("highlightColor", hexColor);
                    // Update preview color at panel level
                    propertyPanel.multiHighlightColor = selectedColor;
                }
            }
        }

        ColorDialog {
            id: multiInactiveColorDialog

            title: i18nc("@title:window", "Inactive Color for All Selected Zones")
            onAccepted: {
                if (editorController) {
                    // Convert QColor to ARGB hex string format (#AARRGGBB)
                    var hexColor = appearanceConstants.colorToArgbHex(selectedColor);
                    editorController.updateSelectedZonesColor("inactiveColor", hexColor);
                    // Update preview color at panel level
                    propertyPanel.multiInactiveColor = selectedColor;
                }
            }
        }

        ColorDialog {
            id: multiBorderColorDialog

            title: i18nc("@title:window", "Border Color for All Selected Zones")
            onAccepted: {
                if (editorController) {
                    // Convert QColor to ARGB hex string format (#AARRGGBB)
                    var hexColor = appearanceConstants.colorToArgbHex(selectedColor);
                    editorController.updateSelectedZonesColor("borderColor", hexColor);
                    // Update preview color at panel level
                    propertyPanel.multiBorderColor = selectedColor;
                }
            }
        }

        component ColorButton: Rectangle {
            id: colorBtn

            required property color color

            signal clicked()

            width: appearanceConstants.colorButtonSize
            height: appearanceConstants.colorButtonSize
            radius: Kirigami.Units.smallSpacing
            border.color: Kirigami.Theme.disabledTextColor
            border.width: 1

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: colorBtn.clicked()
            }

            // Checkerboard pattern for transparency preview
            Canvas {
                anchors.fill: parent
                anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                visible: colorBtn.color.a < 1
                onPaint: {
                    var ctx = getContext("2d");
                    var size = 4;
                    // Use theme-neutral colors for checkerboard pattern (standard for transparency preview)
                    var lightGray = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
                    var white = Qt.rgba(1, 1, 1, 1);
                    for (var x = 0; x < width; x += size) {
                        for (var y = 0; y < height; y += size) {
                            ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white;
                            ctx.fillRect(x, y, size, size);
                        }
                    }
                }
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
                radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
                color: colorBtn.color
            }

        }

    }

    Behavior on opacity {
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

    Behavior on Layout.preferredWidth {
        NumberAnimation {
            duration: 150
            easing.type: Easing.OutCubic
        }

    }

}
