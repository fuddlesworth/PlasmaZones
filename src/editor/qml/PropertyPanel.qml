// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "ColorUtils.js" as ColorUtils

/**
 * @brief Properties panel component for zone editing
 *
 * Displays and allows editing of zone properties when a zone is selected.
 * Includes name, number, appearance settings, and delete action.
 *
 * SRP: This component handles panel layout and coordination.
 * Color utilities extracted to ColorUtils.js
 * UI components extracted to ColorPickerRow, OpacitySliderRow, etc.
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
        if (selectionCount === 0) return "hidden";
        if (selectionCount === 1) return "single";
        return "multiple";
    }

    // Multi-select color preview properties (stored at panel level to avoid context issues)
    property color multiHighlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5)
    property color multiInactiveColor: Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25)
    property color multiBorderColor: Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8)

    // Computed property: true if ALL selected zones have useCustomColors enabled
    readonly property bool allSelectedUseCustomColors: {
        if (!editorController || selectionCount < 2) return false;
        var _ = editorController.zonesVersion;  // Dependency tracking
        return editorController.allSelectedUseCustomColors();
    }

    // Appearance constants
    readonly property real defaultOpacity: 0.5
    readonly property real defaultInactiveOpacity: 0.3
    readonly property int defaultBorderWidth: 2
    readonly property int defaultBorderRadius: 8

    // Layout properties
    Layout.preferredWidth: visible ? 280 : 0
    Layout.maximumWidth: visible ? 280 : 0
    Layout.minimumWidth: 0
    Layout.fillHeight: true
    color: Kirigami.Theme.backgroundColor
    visible: panelMode !== "hidden"
    opacity: visible ? 1 : 0
    z: 50

    // Reset multi-select colors to defaults when entering multi-select mode
    onPanelModeChanged: {
        if (panelMode === "multiple") {
            multiHighlightColor = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);
            multiInactiveColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25);
            multiBorderColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8);
        }
    }

    // Helper functions for extracting zone colors
    function getZoneColor(propertyName) {
        return ColorUtils.extractZoneColor(selectedZone, propertyName, Qt.transparent);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        visible: parent.visible
        spacing: Kirigami.Units.gridUnit

        // Header row with title and close button
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.gridUnit

            Label {
                Layout.fillWidth: true
                text: panelMode === "single"
                    ? i18nc("@title", "Zone Properties")
                    : i18ncp("@title", "1 Zone Selected", "%1 Zones Selected", selectionCount)
                font.bold: true
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.1
            }

            ToolButton {
                icon.name: "window-close"
                onClicked: { if (editorController) editorController.clearSelection(); }
                ToolTip.text: i18nc("@tooltip", "Close panel")
                ToolTip.visible: hovered
                Accessible.name: i18nc("@info:accessibility", "Close properties panel")
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            Kirigami.FormLayout {
                width: parent.width
                wideMode: width > Kirigami.Units.gridUnit * 20

                // ═══════════════════════════════════════════════════════════════
                // MULTI-SELECT APPEARANCE SECTION
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "multiple"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Appearance (All Selected)")
                }

                CheckBox {
                    id: multiUseCustomColorsCheck
                    visible: panelMode === "multiple"
                    Kirigami.FormData.label: i18nc("@label", "Custom colors:")
                    text: i18nc("@option:check", "Enable for all selected")
                    checked: propertyPanel.allSelectedUseCustomColors
                    enabled: editorController !== null
                    Accessible.name: i18nc("@info:accessibility", "Enable custom colors for all selected zones")
                    onToggled: {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("useCustomColors", checked);
                    }
                }

                // Multi-select color pickers
                ColorPickerRow {
                    Kirigami.FormData.label: i18nc("@label", "Highlight:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    baseColor: propertyPanel.multiHighlightColor
                    opacityMultiplier: multiActiveOpacitySlider.opacityValue
                    isMultiMode: true
                    accessibleName: i18nc("@label", "Highlight color picker for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Choose highlight color for all selected zones")
                    onColorButtonClicked: {
                        multiHighlightColorDialog.selectedColor = propertyPanel.multiHighlightColor;
                        multiHighlightColorDialog.open();
                    }
                }

                ColorPickerRow {
                    Kirigami.FormData.label: i18nc("@label", "Inactive:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    baseColor: propertyPanel.multiInactiveColor
                    opacityMultiplier: multiInactiveOpacitySlider.opacityValue
                    isMultiMode: true
                    accessibleName: i18nc("@label", "Inactive color picker for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Choose inactive color for all selected zones")
                    onColorButtonClicked: {
                        multiInactiveColorDialog.selectedColor = propertyPanel.multiInactiveColor;
                        multiInactiveColorDialog.open();
                    }
                }

                ColorPickerRow {
                    Kirigami.FormData.label: i18nc("@label", "Border:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    baseColor: propertyPanel.multiBorderColor
                    isMultiMode: true
                    accessibleName: i18nc("@label", "Border color picker for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Choose border color for all selected zones")
                    onColorButtonClicked: {
                        multiBorderColorDialog.selectedColor = propertyPanel.multiBorderColor;
                        multiBorderColorDialog.open();
                    }
                }

                // Multi-select opacity sliders
                OpacitySliderRow {
                    id: multiActiveOpacitySlider
                    Kirigami.FormData.label: i18nc("@label", "Active opacity:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    opacityValue: propertyPanel.defaultOpacity
                    defaultOpacity: propertyPanel.defaultOpacity
                    sliderEnabled: editorController !== null
                    accessibleName: i18nc("@label", "Active opacity for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Set active opacity for all selected zones (0-100%)")
                    onOpacityEdited: function(value) {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("activeOpacity", value);
                    }
                }

                OpacitySliderRow {
                    id: multiInactiveOpacitySlider
                    Kirigami.FormData.label: i18nc("@label", "Inactive opacity:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    opacityValue: propertyPanel.defaultInactiveOpacity
                    defaultOpacity: propertyPanel.defaultInactiveOpacity
                    sliderEnabled: editorController !== null
                    accessibleName: i18nc("@label", "Inactive opacity for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Set inactive opacity for all selected zones (0-100%)")
                    onOpacityEdited: function(value) {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("inactiveOpacity", value);
                    }
                }

                // Multi-select spinboxes
                AppearanceSpinBox {
                    Kirigami.FormData.label: i18nc("@label", "Border width:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    from: 0; to: 20
                    spinValue: propertyPanel.defaultBorderWidth
                    defaultValue: propertyPanel.defaultBorderWidth
                    spinEnabled: editorController !== null
                    accessibleName: i18nc("@label", "Border width for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Set border width for all selected zones (0-20)")
                    onSpinValueModified: function(newValue) {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("borderWidth", newValue);
                    }
                }

                AppearanceSpinBox {
                    Kirigami.FormData.label: i18nc("@label", "Border radius:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    from: 0; to: 50
                    spinValue: propertyPanel.defaultBorderRadius
                    defaultValue: propertyPanel.defaultBorderRadius
                    spinEnabled: editorController !== null
                    accessibleName: i18nc("@label", "Border radius for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Set corner radius for all selected zones (0-50)")
                    onSpinValueModified: function(newValue) {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("borderRadius", newValue);
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
                    onClicked: { if (editorController) editorController.deleteSelectedZones(); }
                }

                // ═══════════════════════════════════════════════════════════════
                // SINGLE ZONE PROPERTIES
                // ═══════════════════════════════════════════════════════════════
                TextField {
                    id: zoneNameField
                    property string validationError: ""
                    property bool hasError: validationError !== ""
                    property Timer updateTimer

                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Name:")
                    text: selectedZone ? (selectedZone.name || "") : ""
                    enabled: editorController !== null
                    Accessible.name: i18nc("@label", "Zone name")

                    onTextChanged: {
                        if (selectedZoneId && editorController) updateTimer.restart();
                    }

                    onEditingFinished: {
                        updateTimer.stop();
                        validationError = "";
                        if (selectedZoneId && editorController) {
                            var error = editorController.validateZoneName(selectedZoneId, text);
                            if (error !== "") { validationError = error; return; }
                            if (selectedZone && selectedZone.name !== text)
                                editorController.updateZoneName(selectedZoneId, text);
                        }
                    }

                    updateTimer: Timer {
                        interval: 300
                        onTriggered: {
                            if (selectedZoneId && editorController) {
                                zoneNameField.validationError = "";
                                var error = editorController.validateZoneName(selectedZoneId, zoneNameField.text);
                                if (error !== "") { zoneNameField.validationError = error; return; }
                                if (selectedZone && selectedZone.name !== zoneNameField.text)
                                    editorController.updateZoneName(selectedZoneId, zoneNameField.text);
                            }
                        }
                    }

                    background: Rectangle {
                        color: zoneNameField.hasError
                            ? Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.15)
                            : zoneNameField.palette.base
                        radius: Kirigami.Units.smallSpacing
                        border.color: zoneNameField.hasError ? Kirigami.Theme.negativeTextColor : zoneNameField.palette.shadow
                        border.width: zoneNameField.hasError ? 2 : 1
                        Behavior on border.color { ColorAnimation { duration: 100 } }
                        Behavior on border.width { NumberAnimation { duration: 100 } }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: zoneNameField.validationError
                    visible: panelMode === "single" && zoneNameField.hasError
                    color: Kirigami.Theme.negativeTextColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.AlertMessage
                }

                SpinBox {
                    id: zoneNumberSpinBox
                    property string validationError: ""
                    property bool hasError: validationError !== ""

                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Number:")
                    from: 1; to: 99
                    value: selectedZone ? (selectedZone.zoneNumber || 1) : 1
                    enabled: editorController !== null
                    Accessible.name: i18nc("@label", "Zone number")

                    onValueModified: {
                        if (!selectedZoneId || !editorController) return;
                        validationError = "";
                        var error = editorController.validateZoneNumber(selectedZoneId, value);
                        if (error !== "") { validationError = error; return; }
                        editorController.updateZoneNumber(selectedZoneId, value);
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: zoneNumberSpinBox.validationError
                    visible: panelMode === "single" && zoneNumberSpinBox.hasError
                    color: Kirigami.Theme.negativeTextColor
                    font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.AlertMessage
                }

                // ═══════════════════════════════════════════════════════════════
                // SINGLE ZONE APPEARANCE SECTION
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "single"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Appearance")
                }

                CheckBox {
                    id: useCustomColorsCheck
                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Custom colors:")
                    text: i18nc("@option:check", "Use custom colors")
                    checked: selectedZone?.useCustomColors === true
                    // Use explicit boolean to avoid binding loop from null coercion
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@info:accessibility", "Enable custom colors for this zone")
                    onToggled: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "useCustomColors", checked);
                    }
                }

                // Single zone color pickers
                ColorPickerRow {
                    Kirigami.FormData.label: i18nc("@label", "Highlight:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    baseColor: getZoneColor("highlightColor")
                    opacityMultiplier: activeOpacitySlider.opacityValue
                    isMultiMode: false
                    accessibleName: i18nc("@label", "Highlight color picker")
                    toolTipText: i18nc("@info:tooltip", "Choose color for highlighted/active zones")
                    onColorButtonClicked: {
                        var currentColor = getZoneColor("highlightColor");
                        if (currentColor.a === 0)
                            currentColor = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);
                        highlightColorDialog.selectedColor = currentColor;
                        highlightColorDialog.open();
                    }
                }

                ColorPickerRow {
                    Kirigami.FormData.label: i18nc("@label", "Inactive:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    baseColor: getZoneColor("inactiveColor")
                    opacityMultiplier: inactiveOpacitySlider.opacityValue
                    isMultiMode: false
                    accessibleName: i18nc("@label", "Inactive color picker")
                    toolTipText: i18nc("@info:tooltip", "Choose color for non-selected zones")
                    onColorButtonClicked: {
                        var currentColor = getZoneColor("inactiveColor");
                        if (currentColor.a === 0)
                            currentColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25);
                        inactiveColorDialog.selectedColor = currentColor;
                        inactiveColorDialog.open();
                    }
                }

                ColorPickerRow {
                    Kirigami.FormData.label: i18nc("@label", "Border:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    baseColor: getZoneColor("borderColor")
                    isMultiMode: false
                    accessibleName: i18nc("@label", "Border color picker")
                    toolTipText: i18nc("@info:tooltip", "Choose color for zone borders")
                    onColorButtonClicked: {
                        var currentColor = getZoneColor("borderColor");
                        if (currentColor.a === 0)
                            currentColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.8);
                        borderColorDialog.selectedColor = currentColor;
                        borderColorDialog.open();
                    }
                }

                // Single zone opacity sliders
                OpacitySliderRow {
                    id: activeOpacitySlider
                    Kirigami.FormData.label: i18nc("@label", "Active opacity:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    opacityValue: selectedZone?.activeOpacity ?? propertyPanel.defaultOpacity
                    defaultOpacity: propertyPanel.defaultOpacity
                    sliderEnabled: selectedZone !== null && editorController !== null
                    accessibleName: i18nc("@label", "Zone active opacity")
                    toolTipText: i18nc("@info:tooltip", "Adjust zone opacity when highlighted (0-100%)")
                    onOpacityEdited: function(value) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "activeOpacity", value);
                    }
                }

                OpacitySliderRow {
                    id: inactiveOpacitySlider
                    Kirigami.FormData.label: i18nc("@label", "Inactive opacity:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    opacityValue: selectedZone?.inactiveOpacity ?? propertyPanel.defaultInactiveOpacity
                    defaultOpacity: propertyPanel.defaultInactiveOpacity
                    sliderEnabled: selectedZone !== null && editorController !== null
                    accessibleName: i18nc("@label", "Zone inactive opacity")
                    toolTipText: i18nc("@info:tooltip", "Adjust zone opacity when not highlighted (0-100%)")
                    onOpacityEdited: function(value) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "inactiveOpacity", value);
                    }
                }

                // Single zone spinboxes
                AppearanceSpinBox {
                    Kirigami.FormData.label: i18nc("@label", "Border width:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    from: 0; to: 20
                    spinValue: selectedZone?.borderWidth ?? propertyPanel.defaultBorderWidth
                    defaultValue: propertyPanel.defaultBorderWidth
                    spinEnabled: selectedZone !== null && editorController !== null
                    accessibleName: i18nc("@label", "Border width in pixels")
                    toolTipText: i18nc("@info:tooltip", "Set zone border width in pixels (0-20)")
                    onSpinValueModified: function(newValue) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "borderWidth", newValue);
                    }
                }

                AppearanceSpinBox {
                    Kirigami.FormData.label: i18nc("@label", "Border radius:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    from: 0; to: 50
                    spinValue: selectedZone?.borderRadius ?? propertyPanel.defaultBorderRadius
                    defaultValue: propertyPanel.defaultBorderRadius
                    spinEnabled: selectedZone !== null && editorController !== null
                    accessibleName: i18nc("@label", "Border radius in pixels")
                    toolTipText: i18nc("@info:tooltip", "Set zone corner radius in pixels (0-50)")
                    onSpinValueModified: function(newValue) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "borderRadius", newValue);
                    }
                }

                // ═══════════════════════════════════════════════════════════════
                // SINGLE ZONE ACTIONS SECTION
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "single"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Actions")
                }

                Button {
                    visible: panelMode === "single"
                    Layout.fillWidth: true
                    text: i18nc("@action:button", "Delete Zone")
                    icon.name: "edit-delete"
                    enabled: selectedZoneId !== "" && editorController !== null
                    Accessible.name: text
                    Accessible.description: i18nc("@info", "Delete the selected zone")
                    onClicked: { if (selectedZoneId && editorController) editorController.deleteZone(selectedZoneId); }
                }

                // Handle validation errors and selection changes
                Connections {
                    target: editorController
                    enabled: editorController !== null && selectedZoneId !== ""

                    function onSelectedZoneIdChanged() {
                        zoneNameField.updateTimer.stop();
                        zoneNameField.validationError = "";
                        zoneNumberSpinBox.validationError = "";
                        if (selectedZone) zoneNumberSpinBox.value = selectedZone.zoneNumber || 1;
                    }

                    function onZoneNameValidationError(zoneId, error) {
                        if (zoneId === selectedZoneId) {
                            zoneNameField.validationError = error;
                            Qt.callLater(function() {
                                if (selectedZone) zoneNameField.text = selectedZone.name || "";
                            });
                        }
                    }

                    function onZoneNumberValidationError(zoneId, error) {
                        if (zoneId === selectedZoneId) zoneNumberSpinBox.validationError = error;
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════
        // COLOR DIALOGS (Single Zone)
        // ═══════════════════════════════════════════════════════════════
        ZoneColorDialog {
            id: highlightColorDialog
            title: i18nc("@title:window", "Zone Highlight Color")
            editorController: propertyPanel.editorController
            selectedZoneId: propertyPanel.selectedZoneId
            selectedZone: propertyPanel.selectedZone
            colorProperty: "highlightColor"
            onColorAccepted: function(hexColor) {
                if (selectedZoneId && editorController)
                    editorController.updateZoneColor(selectedZoneId, "highlightColor", hexColor);
            }
        }

        ZoneColorDialog {
            id: inactiveColorDialog
            title: i18nc("@title:window", "Zone Inactive Color")
            editorController: propertyPanel.editorController
            selectedZoneId: propertyPanel.selectedZoneId
            selectedZone: propertyPanel.selectedZone
            colorProperty: "inactiveColor"
            onColorAccepted: function(hexColor) {
                if (selectedZoneId && editorController)
                    editorController.updateZoneColor(selectedZoneId, "inactiveColor", hexColor);
            }
        }

        ZoneColorDialog {
            id: borderColorDialog
            title: i18nc("@title:window", "Zone Border Color")
            editorController: propertyPanel.editorController
            selectedZoneId: propertyPanel.selectedZoneId
            selectedZone: propertyPanel.selectedZone
            colorProperty: "borderColor"
            onColorAccepted: function(hexColor) {
                if (selectedZoneId && editorController)
                    editorController.updateZoneColor(selectedZoneId, "borderColor", hexColor);
            }
        }

        // ═══════════════════════════════════════════════════════════════
        // COLOR DIALOGS (Multi-Select)
        // ═══════════════════════════════════════════════════════════════
        ZoneColorDialog {
            id: multiHighlightColorDialog
            title: i18nc("@title:window", "Highlight Color for All Selected Zones")
            isMultiMode: true
            onMultiColorAccepted: function(hexColor, selectedColor) {
                if (editorController) {
                    editorController.updateSelectedZonesColor("highlightColor", hexColor);
                    propertyPanel.multiHighlightColor = selectedColor;
                }
            }
        }

        ZoneColorDialog {
            id: multiInactiveColorDialog
            title: i18nc("@title:window", "Inactive Color for All Selected Zones")
            isMultiMode: true
            onMultiColorAccepted: function(hexColor, selectedColor) {
                if (editorController) {
                    editorController.updateSelectedZonesColor("inactiveColor", hexColor);
                    propertyPanel.multiInactiveColor = selectedColor;
                }
            }
        }

        ZoneColorDialog {
            id: multiBorderColorDialog
            title: i18nc("@title:window", "Border Color for All Selected Zones")
            isMultiMode: true
            onMultiColorAccepted: function(hexColor, selectedColor) {
                if (editorController) {
                    editorController.updateSelectedZonesColor("borderColor", hexColor);
                    propertyPanel.multiBorderColor = selectedColor;
                }
            }
        }
    }

    Behavior on opacity {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }

    Behavior on Layout.preferredWidth {
        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
    }
}
