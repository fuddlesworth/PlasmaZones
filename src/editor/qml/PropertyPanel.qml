// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "ColorUtils.js" as ColorUtils
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Window
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami
import org.phosphor.animation

/**
 * @brief Properties panel component for zone editing
 *
 * Displays and allows editing of zone properties when a zone is selected.
 * Includes name, number, appearance settings, and delete action.
 *
 * Color utilities live in ColorUtils.js; individual controls are in
 * ColorPickerRow, OpacitySliderRow, etc.
 */
Rectangle {
    id: propertyPanel

    // Panel body resolves against the View color set
    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false

    required property var editorController
    required property string selectedZoneId
    required property var selectedZone
    required property int selectionCount
    required property bool hasMultipleSelection
    // Whether the editor is showing its chrome at all. Fullscreen and preview
    // both hide the panel outright, and folding that into `panelShown` (rather
    // than assigning `visible` from the instantiation site) keeps the panel the
    // single owner of its own visibility.
    required property bool chromeVisible
    // The predicate the panel's geometry and opacity animate toward. `visible`
    // is derived from those animated values, so it cannot be the source here.
    readonly property bool panelShown: propertyPanel.chromeVisible && propertyPanel.panelMode !== "hidden"
    // Panel mode: "hidden" (no selection), "single" (one zone), "multiple" (many zones)
    readonly property string panelMode: {
        if (selectionCount === 0)
            return "hidden";

        if (selectionCount === 1)
            return "single";

        return "multiple";
    }
    // The theme-derived zone colours. Multi-select previews start from these
    // until the user picks something else in one of the dialogs, and the
    // single-select dialogs open on them when a zone has no colour of its own.
    readonly property color themeHighlightDefault: Theme.withAlpha(Kirigami.Theme.highlightColor, Theme.zoneHighlightAlpha)
    readonly property color themeInactiveDefault: Theme.withAlpha(Kirigami.Theme.disabledTextColor, Theme.zoneInactiveAlpha)
    readonly property color themeBorderDefault: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    // Multi-select color preview properties (stored at panel level to avoid context issues)
    property color multiHighlightColor: propertyPanel.themeHighlightDefault
    property color multiInactiveColor: propertyPanel.themeInactiveDefault
    property color multiBorderColor: propertyPanel.themeBorderDefault
    // Computed property: true if ALL selected zones have useCustomColors enabled
    readonly property bool allSelectedUseCustomColors: {
        if (!editorController || selectionCount < 2)
            return false;

        var _ = editorController.zonesVersion; // Dependency tracking (zone edits)
        var _2 = editorController.selectedZoneIds; // Dependency tracking (selection composition changes at equal count)
        return editorController.allSelectedUseCustomColors();
    }
    // Appearance constants
    readonly property real defaultOpacity: 0.5
    readonly property real defaultInactiveOpacity: 0.3
    readonly property int defaultBorderWidth: 2
    readonly property int defaultBorderRadius: 8

    // Helper functions for extracting zone colors
    function getZoneColor(propertyName) {
        return ColorUtils.extractZoneColor(selectedZone, propertyName, Qt.transparent);
    }

    // Imperative sync for the geometry controls. The writes below sever these
    // controls' declarative bindings, so from the first call onward this is the
    // only thing that keeps them on the model, and both the selection-change and
    // zonesChanged paths have to re-apply it. Callers that observe the
    // controller's signal directly can pass a fresh zone map, since the panel's
    // own selectedZone property lags that signal.
    function syncGeometryControls(freshZone) {
        const zone = (freshZone && freshZone.id) ? freshZone : selectedZone;
        if (!zone)
            return;

        fixedGeometryCheck.checked = zone.geometryMode === 1;
        fixedXSpin.value = zone.fixedX !== undefined ? zone.fixedX : 0;
        fixedYSpin.value = zone.fixedY !== undefined ? zone.fixedY : 0;
        fixedWidthSpin.value = zone.fixedWidth !== undefined ? zone.fixedWidth : 50;
        fixedHeightSpin.value = zone.fixedHeight !== undefined ? zone.fixedHeight : 50;
    }

    // Same imperative-sync idiom for the appearance controls: QQC2 severs their
    // declarative bindings on the first user interaction (and the writes below
    // finish the job), so without this re-sync a selection change would keep
    // showing the previous zone's values and the next edit would stamp them
    // onto the new zone. The sliders and spinboxes expose sync functions
    // because the severed binding lives on their internal control.
    function syncAppearanceControls(freshZone) {
        const zone = (freshZone && freshZone.id) ? freshZone : selectedZone;
        if (!zone)
            return;

        useCustomColorsCheck.checked = zone.useCustomColors === true;
        activeOpacitySlider.syncOpacity(zone.activeOpacity !== undefined ? zone.activeOpacity : defaultOpacity);
        inactiveOpacitySlider.syncOpacity(zone.inactiveOpacity !== undefined ? zone.inactiveOpacity : defaultInactiveOpacity);
        borderWidthSpinBox.syncValue(zone.borderWidth !== undefined ? zone.borderWidth : defaultBorderWidth);
        borderRadiusSpinBox.syncValue(zone.borderRadius !== undefined ? zone.borderRadius : defaultBorderRadius);
        // The combo's declarative currentIndex binding severs on the first user
        // activation, so re-derive it from the fresh zone map here (same
        // -1 -> 0 / 0 -> 1 / 1 -> 2 mapping as the binding).
        const mode = zone.overlayDisplayMode !== undefined ? zone.overlayDisplayMode : -1;
        zoneOverlayModeCombo.currentIndex = Math.max(0, Math.min(mode + 1, 2));
    }

    // Same imperative-sync idiom for the name field and number spinbox: their
    // bindings die on the first imperative write (selection change), so an
    // undo/redo of a rename/renumber with the same zone still selected must be
    // re-applied here. Focus guards keep an in-progress edit untouched.
    function syncNameAndNumber(freshZone) {
        const zone = (freshZone && freshZone.id) ? freshZone : selectedZone;
        if (!zone)
            return;

        if (!zoneNameField.activeFocus) {
            zoneNameField.text = zone.name || "";
            zoneNameField.validationError = "";
        }
        if (!zoneNumberSpinBox.activeFocus) {
            zoneNumberSpinBox.value = zone.zoneNumber || 1;
            zoneNumberSpinBox.validationError = "";
        }
    }

    // Layout properties
    Layout.preferredWidth: propertyPanel.panelShown ? Kirigami.Units.gridUnit * 16 : 0
    // The animated preferredWidth is the clamp. Binding maximumWidth to the
    // panelShown predicate directly snapped it to 0 in the same pass that
    // started the slide, so the layout collapsed the cell instantly and
    // panel.slideOut animated a zero-width item nobody could see.
    Layout.maximumWidth: propertyPanel.Layout.preferredWidth
    Layout.minimumWidth: 0
    Layout.fillHeight: true
    color: Theme.withAlpha(Kirigami.Theme.backgroundColor, Theme.panelAlpha)
    border.width: 1
    border.color: propertyPanel.themeBorderDefault
    // Drive opacity/width from panelMode and derive visibility from the
    // animated values, so the outgoing legs are actually rendered. Binding
    // `visible` to panelMode directly unrendered the panel in the same pass
    // that started the fade, so panel.fadeOut and panel.slideOut animated
    // something nobody could see. Same shape as the multi-select badge in
    // EditorZone.qml.
    opacity: propertyPanel.panelShown ? 1 : 0
    visible: opacity > 0 || Layout.preferredWidth > 0
    z: 50
    onPanelModeChanged: {
        // Entering "multiple" starts the swatches from the theme again. This is
        // a mode transition, so growing or shrinking a selection that was
        // already multiple (2 zones to 3) keeps whatever the user picked. The
        // color dialogs assign these properties, which severs the binding, so
        // restore the binding rather than the value or the swatches stop
        // following the theme after the first multi-select.
        if (panelMode === "multiple") {
            multiHighlightColor = Qt.binding(function () {
                return propertyPanel.themeHighlightDefault;
            });
            multiInactiveColor = Qt.binding(function () {
                return propertyPanel.themeInactiveDefault;
            });
            multiBorderColor = Qt.binding(function () {
                return propertyPanel.themeBorderDefault;
            });
        }
        // Entering "single" from "multiple" with the same selectedZoneId gets no
        // onSelectedZoneIdChanged, and the onZonesChanged re-sync Connections was
        // disabled while in multi mode, so edits made during multi-select never
        // reached these controls. Re-run the sync trio against the authoritative
        // controller state (the panel's own selectedZone lags), with the same
        // Qt.callLater coalescing idiom the Connections uses. Bail if the lookup
        // is empty: syncing then would fall back to stale panel state.
        if (panelMode === "single" && editorController) {
            const zone = editorController.getZoneById(editorController.selectedZoneId);
            if (zone && zone.id) {
                Qt.callLater(propertyPanel.syncGeometryControls, zone);
                Qt.callLater(propertyPanel.syncAppearanceControls, zone);
                Qt.callLater(propertyPanel.syncNameAndNumber, zone);
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.gridUnit

        // Header row with title and close button
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.gridUnit

            // Accent bar (matches SectionHeader style)
            Rectangle {
                width: Math.round(Kirigami.Units.smallSpacing * 0.75)
                height: panelTitleLabel.height
                color: Kirigami.Theme.highlightColor
                radius: Math.round(Kirigami.Units.smallSpacing / 4)
            }

            Label {
                id: panelTitleLabel

                Layout.fillWidth: true
                text: panelMode === "single" ? i18nc("@title", "Zone Properties") : i18ncp("@title", "%n Zone Selected", "%n Zones Selected", selectionCount)
                font.weight: Font.DemiBold
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.1
            }

            ToolButton {
                icon.name: "window-close"
                onClicked: {
                    if (editorController)
                        editorController.clearSelection();
                }
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
                        // The click severed the declarative binding; restore it
                        // so later zone or selection changes keep the box on the
                        // model (same idiom as the multi color swatches in
                        // onPanelModeChanged).
                        checked = Qt.binding(function () {
                            return propertyPanel.allSelectedUseCustomColors;
                        });
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
                    onOpacityEdited: function (value) {
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
                    onOpacityEdited: function (value) {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("inactiveOpacity", value);
                    }
                }

                // Multi-select spinboxes
                AppearanceSpinBox {
                    Kirigami.FormData.label: i18nc("@label", "Border width:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    from: 0
                    to: 20
                    spinValue: propertyPanel.defaultBorderWidth
                    defaultValue: propertyPanel.defaultBorderWidth
                    spinEnabled: editorController !== null
                    accessibleName: i18nc("@label", "Border width for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Set border width for all selected zones (0-20)")
                    onSpinValueModified: function (newValue) {
                        if (editorController)
                            editorController.updateSelectedZonesAppearance("borderWidth", newValue);
                    }
                }

                AppearanceSpinBox {
                    Kirigami.FormData.label: i18nc("@label", "Border radius:")
                    visible: panelMode === "multiple" && multiUseCustomColorsCheck.checked
                    from: 0
                    to: 50
                    spinValue: propertyPanel.defaultBorderRadius
                    defaultValue: propertyPanel.defaultBorderRadius
                    spinEnabled: editorController !== null
                    accessibleName: i18nc("@label", "Border radius for all selected zones")
                    toolTipText: i18nc("@info:tooltip", "Set corner radius for all selected zones (0-50)")
                    onSpinValueModified: function (newValue) {
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
                    Accessible.description: i18ncp("@info", "Delete %n selected zone", "Delete %n selected zones", selectionCount)
                    onClicked: {
                        if (editorController)
                            editorController.deleteSelectedZones();
                    }
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
                    // Mirrors PlasmaZones::MaxLayoutNameLength (core/constants.h),
                    // same client-side cap as TopBar's layout name field.
                    maximumLength: 40
                    text: selectedZone ? (selectedZone.name || "") : ""
                    enabled: editorController !== null
                    Accessible.name: i18nc("@label", "Zone name")
                    onTextChanged: {
                        // Only user edits (field focused) arm the auto-commit
                        // timer. Programmatic writes — the declarative binding
                        // on selection change and syncNameAndNumber() — also
                        // fire onTextChanged, and a legacy >40-char zone name
                        // truncated by maximumLength would otherwise auto-commit
                        // a rename (undo entry + unsaved mark) on mere selection.
                        // The stop() also kills a pending timer from a previous
                        // zone so its text can't commit under the new zone's id.
                        if (activeFocus && selectedZoneId && editorController)
                            updateTimer.restart();
                        else
                            updateTimer.stop();
                    }
                    // textEdited fires only on user input. An error describes the
                    // text that was rejected, so the first keystroke past it drops
                    // the message rather than leaving a stale complaint on screen.
                    onTextEdited: {
                        validationError = "";
                    }
                    onEditingFinished: {
                        updateTimer.stop();
                        validationError = "";
                        if (selectedZoneId && editorController) {
                            var error = editorController.validateZoneName(selectedZoneId, text);
                            if (error !== "") {
                                validationError = error;
                                return;
                            }
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
                                if (error !== "") {
                                    zoneNameField.validationError = error;
                                    return;
                                }
                                if (selectedZone && selectedZone.name !== zoneNameField.text)
                                    editorController.updateZoneName(selectedZoneId, zoneNameField.text);
                            }
                        }
                    }

                    background: Rectangle {
                        color: zoneNameField.hasError ? Theme.withAlpha(Kirigami.Theme.negativeTextColor, 0.15) : zoneNameField.palette.base
                        radius: Kirigami.Units.smallSpacing
                        border.color: zoneNameField.hasError ? Kirigami.Theme.negativeTextColor : zoneNameField.palette.shadow
                        border.width: zoneNameField.hasError ? 2 : 1

                        Behavior on border.color {
                            PhosphorMotionAnimation {
                                profile: "widget.tint.fast"
                            }
                        }

                        Behavior on border.width {
                            PhosphorMotionAnimation {
                                profile: "widget.tint.fast"
                            }
                        }
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
                    from: 1
                    to: 99
                    value: selectedZone ? (selectedZone.zoneNumber || 1) : 1
                    enabled: editorController !== null
                    Accessible.name: i18nc("@label", "Zone number")
                    onValueModified: {
                        if (!selectedZoneId || !editorController)
                            return;

                        validationError = "";
                        var error = editorController.validateZoneNumber(selectedZoneId, value);
                        if (error !== "") {
                            validationError = error;
                            return;
                        }
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
                // SINGLE ZONE GEOMETRY SECTION
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "single"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Geometry")
                }

                CheckBox {
                    id: fixedGeometryCheck

                    visible: panelMode === "single"
                    Kirigami.FormData.label: i18nc("@label", "Mode:")
                    text: i18nc("@option:check", "Fixed pixel size")
                    checked: selectedZone ? (selectedZone.geometryMode === 1) : false
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@info:accessibility", "Toggle fixed pixel geometry mode")
                    Accessible.description: i18nc("@info:accessibility", "When enabled, zone uses absolute pixel coordinates instead of relative percentages")
                    onToggled: {
                        if (selectedZoneId && editorController)
                            editorController.toggleZoneGeometryMode(selectedZoneId);
                    }
                }

                SpinBox {
                    id: fixedXSpin

                    visible: panelMode === "single" && fixedGeometryCheck.checked
                    Kirigami.FormData.label: i18nc("@label", "X:")
                    from: 0
                    to: editorController ? editorController.targetScreenSize.width : 99999
                    value: selectedZone ? (selectedZone.fixedX !== undefined ? selectedZone.fixedX : 0) : 0
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@label", "X position in pixels")
                    onValueModified: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneFixedGeometry(selectedZoneId, value, fixedYSpin.value, fixedWidthSpin.value, fixedHeightSpin.value);
                    }
                }

                SpinBox {
                    id: fixedYSpin

                    visible: panelMode === "single" && fixedGeometryCheck.checked
                    Kirigami.FormData.label: i18nc("@label", "Y:")
                    from: 0
                    to: editorController ? editorController.targetScreenSize.height : 99999
                    value: selectedZone ? (selectedZone.fixedY !== undefined ? selectedZone.fixedY : 0) : 0
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@label", "Y position in pixels")
                    onValueModified: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneFixedGeometry(selectedZoneId, fixedXSpin.value, value, fixedWidthSpin.value, fixedHeightSpin.value);
                    }
                }

                SpinBox {
                    id: fixedWidthSpin

                    visible: panelMode === "single" && fixedGeometryCheck.checked
                    Kirigami.FormData.label: i18nc("@label", "Width:")
                    from: 50
                    to: editorController ? editorController.targetScreenSize.width : 99999
                    value: selectedZone ? (selectedZone.fixedWidth !== undefined ? selectedZone.fixedWidth : 50) : 50
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@label", "Width in pixels")
                    onValueModified: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneFixedGeometry(selectedZoneId, fixedXSpin.value, fixedYSpin.value, value, fixedHeightSpin.value);
                    }
                }

                SpinBox {
                    id: fixedHeightSpin

                    visible: panelMode === "single" && fixedGeometryCheck.checked
                    Kirigami.FormData.label: i18nc("@label", "Height:")
                    from: 50
                    to: editorController ? editorController.targetScreenSize.height : 99999
                    value: selectedZone ? (selectedZone.fixedHeight !== undefined ? selectedZone.fixedHeight : 50) : 50
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@label", "Height in pixels")
                    onValueModified: {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneFixedGeometry(selectedZoneId, fixedXSpin.value, fixedYSpin.value, fixedWidthSpin.value, value);
                    }
                }

                // Sync geometry, name, and number controls when zone data changes
                // (undo/redo, external updates). A user click does not sever a
                // binding (the control writes its value from C++), but this
                // function's own imperative writes do, so once it has run these
                // controls have no bindings left and it is the only thing keeping
                // them current. Includes the mode checkbox, and is not gated on
                // fixedGeometryCheck.checked, for that reason.
                Connections {
                    function onZonesChanged() {
                        // Qt.callLater avoids binding loops, and the stable function
                        // references let it coalesce bursts of zonesChanged into one sync.
                        Qt.callLater(propertyPanel.syncGeometryControls);
                        Qt.callLater(propertyPanel.syncNameAndNumber);
                        Qt.callLater(propertyPanel.syncAppearanceControls);
                    }

                    target: editorController
                    enabled: panelMode === "single" && editorController !== null
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
                    checked: selectedZone && selectedZone.useCustomColors === true
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
                            currentColor = propertyPanel.themeHighlightDefault;

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
                            currentColor = propertyPanel.themeInactiveDefault;

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
                            currentColor = propertyPanel.themeBorderDefault;

                        borderColorDialog.selectedColor = currentColor;
                        borderColorDialog.open();
                    }
                }

                // Single zone opacity sliders
                OpacitySliderRow {
                    id: activeOpacitySlider

                    Kirigami.FormData.label: i18nc("@label", "Active opacity:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    opacityValue: selectedZone ? selectedZone.activeOpacity : propertyPanel.defaultOpacity
                    defaultOpacity: propertyPanel.defaultOpacity
                    sliderEnabled: selectedZone !== null && editorController !== null
                    accessibleName: i18nc("@label", "Zone active opacity")
                    toolTipText: i18nc("@info:tooltip", "Adjust zone opacity when highlighted (0-100%)")
                    onOpacityEdited: function (value) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "activeOpacity", value);
                    }
                }

                OpacitySliderRow {
                    id: inactiveOpacitySlider

                    Kirigami.FormData.label: i18nc("@label", "Inactive opacity:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    opacityValue: selectedZone ? selectedZone.inactiveOpacity : propertyPanel.defaultInactiveOpacity
                    defaultOpacity: propertyPanel.defaultInactiveOpacity
                    sliderEnabled: selectedZone !== null && editorController !== null
                    accessibleName: i18nc("@label", "Zone inactive opacity")
                    toolTipText: i18nc("@info:tooltip", "Adjust zone opacity when not highlighted (0-100%)")
                    onOpacityEdited: function (value) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "inactiveOpacity", value);
                    }
                }

                // Single zone spinboxes
                AppearanceSpinBox {
                    id: borderWidthSpinBox

                    Kirigami.FormData.label: i18nc("@label", "Border width:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    from: 0
                    to: 20
                    spinValue: selectedZone ? selectedZone.borderWidth : propertyPanel.defaultBorderWidth
                    defaultValue: propertyPanel.defaultBorderWidth
                    spinEnabled: Boolean(selectedZone) && Boolean(editorController)
                    accessibleName: i18nc("@label", "Border width in pixels")
                    toolTipText: i18nc("@info:tooltip", "Set zone border width in pixels (0-20)")
                    onSpinValueModified: function (newValue) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "borderWidth", newValue);
                    }
                }

                AppearanceSpinBox {
                    id: borderRadiusSpinBox

                    Kirigami.FormData.label: i18nc("@label", "Border radius:")
                    visible: panelMode === "single" && selectedZone !== null && useCustomColorsCheck.checked
                    from: 0
                    to: 50
                    spinValue: selectedZone ? selectedZone.borderRadius : propertyPanel.defaultBorderRadius
                    defaultValue: propertyPanel.defaultBorderRadius
                    spinEnabled: Boolean(selectedZone) && Boolean(editorController)
                    accessibleName: i18nc("@label", "Border radius in pixels")
                    toolTipText: i18nc("@info:tooltip", "Set zone corner radius in pixels (0-50)")
                    onSpinValueModified: function (newValue) {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "borderRadius", newValue);
                    }
                }

                // ═══════════════════════════════════════════════════════════════
                // SINGLE ZONE OVERLAY STYLE
                // ═══════════════════════════════════════════════════════════════
                Kirigami.Separator {
                    visible: panelMode === "single"
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18nc("@title", "Overlay")
                }

                ComboBox {
                    id: zoneOverlayModeCombo

                    visible: panelMode === "single"
                    Layout.fillWidth: true
                    Kirigami.FormData.label: i18nc("@label", "Style:")
                    implicitContentWidthPolicy: ComboBox.WidestTextWhenCompleted
                    model: [i18nc("@item:inlistbox overlay mode", "Use layout default"), i18nc("@item:inlistbox", "Full zone highlight"), i18nc("@item:inlistbox", "Compact preview")]
                    currentIndex: {
                        if (!selectedZone)
                            return 0;

                        let mode = selectedZone.overlayDisplayMode !== undefined ? selectedZone.overlayDisplayMode : -1;
                        return Math.max(0, Math.min(mode + 1, 2)); // -1 -> 0 (default), 0 -> 1, 1 -> 2
                    }
                    enabled: Boolean(selectedZone) && Boolean(editorController)
                    Accessible.name: i18nc("@label", "Zone overlay display mode")
                    Accessible.description: i18nc("@info:accessibility", "Override the overlay style for this zone only")
                    onActivated: index => {
                        if (selectedZoneId && editorController)
                            editorController.updateZoneAppearance(selectedZoneId, "overlayDisplayMode", index - 1);
                    }

                    popup: T.Popup {
                        y: zoneOverlayModeCombo.height
                        width: Math.max(zoneOverlayModeCombo.width, zoneOverlayModeCombo.implicitContentWidth + Kirigami.Units.gridUnit * 3)
                        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, (zoneOverlayModeCombo.Window.window ? zoneOverlayModeCombo.Window.window.height : 600) - topMargin - bottomMargin)
                        topMargin: Kirigami.Units.smallSpacing
                        bottomMargin: Kirigami.Units.smallSpacing
                        padding: 1

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: zoneOverlayModeCombo.delegateModel
                            currentIndex: zoneOverlayModeCombo.highlightedIndex
                            highlightMoveDuration: 0
                        }

                        background: Rectangle {
                            color: Kirigami.Theme.backgroundColor
                            border.color: propertyPanel.themeBorderDefault
                            border.width: 1
                            radius: Kirigami.Units.smallSpacing
                        }
                    }

                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index
                        readonly property bool isCurrentSelection: zoneOverlayModeCombo.currentIndex === index

                        width: zoneOverlayModeCombo.popup.availableWidth
                        highlighted: zoneOverlayModeCombo.highlightedIndex === index

                        background: Rectangle {
                            color: parent.highlighted ? Kirigami.Theme.highlightColor : parent.isCurrentSelection ? Theme.withAlpha(Kirigami.Theme.highlightColor, 0.15) : Kirigami.Theme.backgroundColor
                        }

                        contentItem: Label {
                            text: modelData
                            color: parent.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor
                            font.bold: parent.highlighted || parent.isCurrentSelection
                            elide: Text.ElideRight
                            verticalAlignment: Text.AlignVCenter
                        }
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
                    onClicked: {
                        if (selectedZoneId && editorController)
                            editorController.deleteZone(selectedZoneId);
                    }
                }

                // Handle validation errors and selection changes
                Connections {
                    function onSelectedZoneIdChanged() {
                        // This Connections fires on the CONTROLLER's signal,
                        // before EditorWindow's copy of the id (which feeds
                        // propertyPanel.selectedZoneId/selectedZone) updates in
                        // its own, later handler of the same signal. Inside this
                        // handler the panel's properties are one step stale, so
                        // read the authoritative controller state directly.
                        const id = editorController.selectedZoneId;
                        if (id === "")
                            return;

                        // Re-sync every editable control imperatively: their
                        // declarative bindings die on the first user edit (QQC2)
                        // or imperative sync, after which a selection change
                        // would keep showing the previous zone's values and any
                        // edit would stamp those stale values onto the new zone.
                        // If the lookup comes back empty the zone is gone or
                        // not yet published; syncing anyway would fall back to
                        // the panel's stale selectedZone and stamp the previous
                        // zone's values, so do nothing at all.
                        const zone = editorController.getZoneById(id);
                        if (!(zone && zone.id))
                            return;

                        zoneNameField.text = zone.name || "";
                        zoneNumberSpinBox.value = zone.zoneNumber || 1;
                        propertyPanel.syncGeometryControls(zone);
                        propertyPanel.syncAppearanceControls(zone);
                        zoneNameField.updateTimer.stop();
                        zoneNameField.validationError = "";
                        zoneNumberSpinBox.validationError = "";
                    }

                    function onZoneNameValidationError(zoneId, error) {
                        if (zoneId === selectedZoneId) {
                            zoneNameField.validationError = error;
                            Qt.callLater(function () {
                                // Only reset to the committed name while the field
                                // is not being edited; mid-retype input stays put
                                // (same guard as syncNameAndNumber). The error
                                // survives the revert: it explains why the name the
                                // user typed is gone, and this is the only feedback
                                // a rename rejected on blur ever gets. The next
                                // keystroke clears it.
                                if (selectedZone && !zoneNameField.activeFocus)
                                    zoneNameField.text = selectedZone.name || "";
                            });
                        }
                    }

                    function onZoneNumberValidationError(zoneId, error) {
                        if (zoneId === selectedZoneId)
                            zoneNumberSpinBox.validationError = error;
                    }

                    target: editorController
                    enabled: editorController !== null
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
            onColorAccepted: function (hexColor) {
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
            onColorAccepted: function (hexColor) {
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
            onColorAccepted: function (hexColor) {
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
            editorController: propertyPanel.editorController
            isMultiMode: true
            onMultiColorAccepted: function (hexColor, selectedColor) {
                if (propertyPanel.editorController) {
                    propertyPanel.editorController.updateSelectedZonesColor("highlightColor", hexColor);
                    propertyPanel.multiHighlightColor = selectedColor;
                }
            }
        }

        ZoneColorDialog {
            id: multiInactiveColorDialog

            title: i18nc("@title:window", "Inactive Color for All Selected Zones")
            editorController: propertyPanel.editorController
            isMultiMode: true
            onMultiColorAccepted: function (hexColor, selectedColor) {
                if (propertyPanel.editorController) {
                    propertyPanel.editorController.updateSelectedZonesColor("inactiveColor", hexColor);
                    propertyPanel.multiInactiveColor = selectedColor;
                }
            }
        }

        ZoneColorDialog {
            id: multiBorderColorDialog

            title: i18nc("@title:window", "Border Color for All Selected Zones")
            editorController: propertyPanel.editorController
            isMultiMode: true
            onMultiColorAccepted: function (hexColor, selectedColor) {
                if (propertyPanel.editorController) {
                    propertyPanel.editorController.updateSelectedZonesColor("borderColor", hexColor);
                    propertyPanel.multiBorderColor = selectedColor;
                }
            }
        }
    }

    Behavior on opacity {
        PhosphorMotionAnimation {
            // Direction is taken from the same `panelShown` predicate that
            // drives `opacity` above. Reading the animated `opacity`, or the
            // `visible` now derived from it, would re-evaluate during the
            // Behavior and flip the leg mid-animation.
            profile: propertyPanel.panelShown ? "panel.fadeIn" : "panel.fadeOut"
            durationOverride: Kirigami.Units.longDuration
        }
    }

    Behavior on Layout.preferredWidth {
        PhosphorMotionAnimation {
            // Direction is taken from `panelShown` (the same predicate driving
            // `Layout.preferredWidth` above). slideIn when growing into view,
            // slideOut when collapsing out.
            profile: propertyPanel.panelShown ? "panel.slideIn" : "panel.slideOut"
            durationOverride: Kirigami.Units.longDuration
        }
    }
}
