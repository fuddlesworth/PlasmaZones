// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Zone selector popup settings section with per-monitor overrides
 *
 * Extracted component containing the zone selector popup configuration:
 * info message, enable toggle, position & trigger, layout arrangement, and
 * preview size cards. The three per-monitor cards carry a header scope chip,
 * so per-monitor overrides are scoped to the monitor picked there (shared
 * app-wide scope). The chip rests on "All Monitors" until a specific output
 * is picked from its popover.
 */
ColumnLayout {
    // ═══════════════════════════════════════════════════════════════════════
    // PER-MONITOR SETTINGS
    // ═══════════════════════════════════════════════════════════════════════

    id: root

    required property var appSettings
    // Controller exposing the per-screen / scope methods (scopeScreenName,
    // hasPerScreenZoneSelectorSettings, clearPerScreenZoneSelectorSettings).
    // Required: the raw ISettings object passed as `appSettings` does NOT carry
    // the scope chip's Q_INVOKABLEs, so the two must be supplied independently.
    required property var controller
    // Per-monitor zone-selector overrides are rule-backed (the whole per-screen
    // store folds onto a screen-scoped rule of generic SetZoneSelectorProperty
    // actions). Read + write both route through the RuleController; bumped to
    // re-read the value bindings after a write / scope switch / reset.
    readonly property var ruleController: root.controller.rulesPage
    property int zsReloadTick: 0
    required property QtObject constants
    // Screen aspect ratio for preview calculations (with safety check)
    property real screenAspectRatio: 16 / 9
    readonly property real safeAspectRatio: screenAspectRatio > 0 ? screenAspectRatio : (16 / 9)
    // Effective values that resolve per-screen > global
    readonly property int effectivePosition: settingValue("Position", appSettings.zoneSelectorPosition)
    readonly property int effectiveLayoutMode: settingValue("LayoutMode", appSettings.zoneSelectorLayoutMode)
    readonly property int effectiveSizeMode: settingValue("SizeMode", appSettings.zoneSelectorSizeMode)
    readonly property int effectiveGridColumns: settingValue("GridColumns", appSettings.zoneSelectorGridColumns)
    readonly property int effectiveMaxRows: settingValue("MaxRows", appSettings.zoneSelectorMaxRows)
    readonly property int effectivePreviewWidth: {
        var sm = effectiveSizeMode;
        if (sm === 0)
            return Math.round(root.constants.zoneSelectorPreviewMedium * (safeAspectRatio / (16 / 9)));

        return settingValue("PreviewWidth", appSettings.zoneSelectorPreviewWidth);
    }
    readonly property int effectivePreviewHeight: {
        var sm = effectiveSizeMode;
        if (sm === 0)
            return Math.round(effectivePreviewWidth / safeAspectRatio);

        return settingValue("PreviewHeight", appSettings.zoneSelectorPreviewHeight);
    }
    readonly property int effectiveTriggerDistance: settingValue("TriggerDistance", appSettings.zoneSelectorTriggerDistance)

    // Read a zone-selector property honouring the current scope: global returns
    // the passed-in value; a monitor scope reads its override rule's matching
    // SetZoneSelectorProperty action, falling back to global when unset. Reading
    // zsReloadTick makes every effective* binding re-evaluate on rule changes.
    function settingValue(key, globalValue) {
        root.zsReloadTick;
        const scope = psHelper.selectedScreenName;
        if (scope === "")
            return globalValue;
        const id = root.controller.perScreenZoneSelectorRuleId(scope);
        if (!id)
            return globalValue;
        const rule = root.ruleController.ruleJson(id);
        if (rule && rule.id) {
            const actions = rule.actions || [];
            for (var i = 0; i < actions.length; ++i) {
                if (actions[i].type === "setZoneSelectorProperty" && actions[i].property === key && actions[i].value !== undefined)
                    return actions[i].value;
            }
        }
        return globalValue;
    }

    // Merge value into the SetZoneSelectorProperty action for @p property on
    // ruleObj (create the action if absent).
    function _applyZsAction(ruleObj, property, value) {
        const actions = (ruleObj.actions || []).slice();
        var found = false;
        for (var i = 0; i < actions.length; ++i) {
            if (actions[i].type === "setZoneSelectorProperty" && actions[i].property === property) {
                var updated = Object.assign({}, actions[i]);
                updated.value = value;
                actions[i] = updated;
                found = true;
                break;
            }
        }
        if (!found)
            actions.push({
                "type": "setZoneSelectorProperty",
                "property": property,
                "value": value
            });
        ruleObj.actions = actions;
        return ruleObj;
    }

    // Write a zone-selector property honouring the current scope. Global calls the
    // config setter; a monitor scope find-or-creates that monitor's rule and sets
    // the matching SetZoneSelectorProperty action (other properties inherit global).
    function writeSetting(key, value, globalSetter) {
        const scope = psHelper.selectedScreenName;
        if (scope === "") {
            globalSetter(value);
            return;
        }
        const id = root.controller.perScreenZoneSelectorRuleId(scope);
        if (!id)
            return;
        const canonical = root.controller.canonicalScreenId(scope);
        var rule = root.ruleController.ruleJson(id);
        if (!rule || !rule.id) {
            rule = {
                "id": id,
                "name": i18n("Zone selector (%1)", canonical),
                "enabled": true,
                "match": {
                    "field": "screenId",
                    "op": "equals",
                    "value": canonical
                },
                "actions": []
            };
            root._applyZsAction(rule, key, value);
            root.ruleController.addRuleFromJson(rule);
        } else {
            root._applyZsAction(rule, key, value);
            root.ruleController.updateRuleFromJson(rule);
        }
        root.zsReloadTick++;
    }

    // Re-read the value bindings on rule reload, scope change, or a per-screen
    // override add / clear (the scope chip reset surfaces as perScreenOverridesChanged).
    Connections {
        target: root.ruleController
        function onRulesLoaded() {
            root.zsReloadTick++;
        }
    }
    Connections {
        target: root.controller
        function onScopeScreenNameChanged() {
            root.zsReloadTick++;
        }
        function onPerScreenOverridesChanged() {
            root.zsReloadTick++;
        }
    }

    spacing: Kirigami.Units.largeSpacing

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: root.controller
        // Shared app-wide scope — a monitor picked anywhere stays picked here.
        selectedScreenName: root.controller.scopeScreenName
        getterMethod: "getPerScreenZoneSelectorSettings"
        setterMethod: "setPerScreenZoneSelectorSetting"
    }

    // Info message
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        type: Kirigami.MessageType.Information
        text: i18n("The zone selector popup appears when dragging windows to screen edges, allowing quick layout selection.")
        visible: true
    }

    // Enable toggle. Intentionally app-wide (not per-monitor): it is the global
    // gate for the feature. Only the three cards below (Position & Trigger,
    // Layout, Preview Size) carry the header scope chip and store per-monitor
    // overrides.
    SettingsRow {
        title: i18n("Zone selector popup")
        searchAnchor: "zoneSelectorEnabled"
        description: i18n("Show a layout picker when dragging windows to screen edges")

        SettingsSwitch {
            checked: appSettings.zoneSelectorEnabled
            accessibleName: i18n("Enable zone selector popup")
            onToggled: function (newValue) {
                appSettings.zoneSelectorEnabled = newValue;
            }
        }
    }

    // Position & Trigger card - wrapped in Item for stable sizing
    Item {
        Layout.fillWidth: true
        implicitHeight: positionCard.implicitHeight

        SettingsCard {
            id: positionCard

            anchors.fill: parent
            enabled: appSettings.zoneSelectorEnabled
            headerText: i18n("Position & Trigger")
            searchAnchor: "positionTrigger"
            collapsible: true
            scopeEnabled: true
            scopeAppSettings: root.controller
            scopeHasOverridesMethod: "hasPerScreenZoneSelectorSettings"
            scopeClearerMethod: "clearPerScreenZoneSelectorSettings"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Centered position picker with description
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: positionPicker.height + Kirigami.Units.gridUnit * 2

                    PositionPicker {
                        id: positionPicker

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        position: root.effectivePosition
                        enabled: appSettings.zoneSelectorEnabled
                        onPositionSelected: function (newPosition) {
                            root.writeSetting("Position", newPosition, function (v) {
                                appSettings.zoneSelectorPosition = v;
                            });
                        }
                    }

                    Label {
                        id: positionDescription

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: positionPicker.bottom
                        anchors.topMargin: Kirigami.Units.smallSpacing
                        text: i18n("Choose where the popup appears on screen")
                        opacity: 0.7
                        font: Kirigami.Theme.smallFont
                    }
                }

                SettingsSeparator {}

                // Trigger distance
                SettingsRow {
                    title: i18n("Trigger distance")
                    searchAnchor: "triggerDistance"
                    description: i18n("How close to the screen edge before the popup appears")

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Slider {
                            id: triggerSlider

                            Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                            from: root.constants.zoneSelectorTriggerMin
                            to: root.constants.zoneSelectorTriggerMax
                            stepSize: 10
                            Accessible.name: i18n("Trigger distance")
                            onMoved: root.writeSetting("TriggerDistance", value, function (v) {
                                appSettings.zoneSelectorTriggerDistance = v;
                            })

                            Binding on value {
                                value: root.effectiveTriggerDistance
                                when: !triggerSlider.pressed
                                restoreMode: Binding.RestoreNone
                            }
                        }

                        Label {
                            text: root.effectiveTriggerDistance + " px"
                            Layout.preferredWidth: root.constants.sliderValueLabelWidth + Kirigami.Units.largeSpacing
                            horizontalAlignment: Text.AlignRight
                            font: Kirigami.Theme.fixedWidthFont
                        }
                    }
                }
            }
        }
    }

    // Layout Arrangement card - wrapped in Item for stable sizing
    Item {
        Layout.fillWidth: true
        implicitHeight: layoutCard.implicitHeight

        SettingsCard {
            id: layoutCard

            anchors.fill: parent
            enabled: appSettings.zoneSelectorEnabled
            headerText: i18n("Layout Arrangement")
            searchAnchor: "layoutArrangement"
            collapsible: true
            scopeEnabled: true
            scopeAppSettings: root.controller
            scopeHasOverridesMethod: "hasPerScreenZoneSelectorSettings"
            scopeClearerMethod: "clearPerScreenZoneSelectorSettings"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Arrangement")
                    searchAnchor: "arrangement"
                    description: i18n("How layout previews are arranged in the popup")

                    WideComboBox {
                        id: zoneSelectorLayoutModeCombo

                        Accessible.name: i18n("Arrangement")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            {
                                "text": i18n("Grid"),
                                "value": 0
                            },
                            {
                                "text": i18n("Horizontal"),
                                "value": 1
                            },
                            {
                                "text": i18n("Vertical"),
                                "value": 2
                            }
                        ]
                        currentIndex: Math.max(0, indexOfValue(root.effectiveLayoutMode))
                        onActivated: root.writeSetting("LayoutMode", currentValue, function (v) {
                            appSettings.zoneSelectorLayoutMode = v;
                        })
                    }
                }

                SettingsSeparator {
                    visible: root.effectiveLayoutMode === 0
                }

                SettingsRow {
                    visible: root.effectiveLayoutMode === 0
                    title: i18n("Grid columns")
                    searchAnchor: "gridColumns"
                    description: i18n("Number of layout previews per row")

                    SettingsSpinBox {
                        from: root.constants.zoneSelectorGridColumnsMin
                        to: root.constants.zoneSelectorGridColumnsMax
                        value: root.effectiveGridColumns
                        unitText: ""
                        onValueModified: value => {
                            return root.writeSetting("GridColumns", value, function (v) {
                                appSettings.zoneSelectorGridColumns = v;
                            });
                        }
                    }
                }

                SettingsSeparator {
                    visible: root.effectiveLayoutMode === 0
                }

                SettingsRow {
                    visible: root.effectiveLayoutMode === 0
                    title: i18n("Max visible rows")
                    searchAnchor: "maxVisibleRows"
                    description: i18n("Scrolling enabled when more rows exist")

                    SettingsSpinBox {
                        from: root.constants.zoneSelectorMaxRowsMin
                        to: root.constants.zoneSelectorMaxRowsMax
                        value: root.effectiveMaxRows
                        unitText: ""
                        onValueModified: value => {
                            return root.writeSetting("MaxRows", value, function (v) {
                                appSettings.zoneSelectorMaxRows = v;
                            });
                        }
                    }
                }
            }
        }
    }

    // Preview Size card - wrapped in Item for stable sizing
    Item {
        Layout.fillWidth: true
        implicitHeight: previewCard.implicitHeight

        SettingsCard {
            id: previewCard

            anchors.fill: parent
            enabled: appSettings.zoneSelectorEnabled
            headerText: i18n("Preview Size")
            searchAnchor: "previewSize"
            collapsible: true
            scopeEnabled: true
            scopeAppSettings: root.controller
            scopeHasOverridesMethod: "hasPerScreenZoneSelectorSettings"
            scopeClearerMethod: "clearPerScreenZoneSelectorSettings"

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Live preview - centered
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.effectivePreviewHeight + Kirigami.Units.gridUnit * 3

                    // Preview container
                    Item {
                        id: sizePreviewContainer

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: root.effectivePreviewWidth
                        height: root.effectivePreviewHeight

                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            radius: Kirigami.Units.smallSpacing * 1.5
                            border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)
                            border.width: 1

                            // Sample zones. The small integer literals below
                            // (2px inset, 1px inter-zone gap, 2px corner radius,
                            // 1px borders, the 20px label-visibility threshold)
                            // are intentional fixed drawing dimensions of this
                            // miniature preview — diagram detail, not layout
                            // spacing, so they are deliberately not theme-scaled.
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
                                            font.weight: Font.DemiBold
                                            color: Kirigami.Theme.textColor
                                            opacity: 0.5
                                            visible: parent.width >= 20
                                        }
                                    }
                                }
                            }
                        }

                        // Size label
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: Kirigami.Units.smallSpacing
                            text: root.effectivePreviewWidth + " × " + root.effectivePreviewHeight + " px"
                            font: Kirigami.Theme.fixedWidthFont
                            opacity: 0.7
                        }
                    }
                }

                // Size selection - segmented button style
                RowLayout {
                    // Custom (width doesn't match preset)

                    id: sizeButtonRow

                    // Track explicit Custom mode selection
                    property bool customModeActive: false
                    // Track which size is selected
                    // 0=Auto, 1=Small, 2=Medium, 3=Large, 4=Custom. A preset is
                    // considered selected when the width is within half a slider
                    // step (stepSize 10 → 5px) of that preset's value.
                    readonly property int presetMatchTolerance: 5
                    property int selectedSize: {
                        if (root.effectiveSizeMode === 0)
                            return 0;

                        // Auto
                        if (customModeActive)
                            return 4;

                        // Explicit Custom selection
                        var w = root.effectivePreviewWidth;
                        if (Math.abs(w - root.constants.zoneSelectorPreviewSmall) <= presetMatchTolerance)
                            return 1;

                        // Small
                        if (Math.abs(w - root.constants.zoneSelectorPreviewMedium) <= presetMatchTolerance)
                            return 2;

                        // Medium
                        if (Math.abs(w - root.constants.zoneSelectorPreviewLarge) <= presetMatchTolerance)
                            return 3;

                        // Large
                        return 4;
                    }

                    Layout.alignment: Qt.AlignHCenter
                    spacing: 0

                    Button {
                        text: i18n("Auto")
                        flat: parent.selectedSize !== 0
                        highlighted: parent.selectedSize === 0
                        onClicked: {
                            sizeButtonRow.customModeActive = false;
                            root.writeSetting("SizeMode", 0, function (v) {
                                appSettings.zoneSelectorSizeMode = v;
                            });
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("Size scales automatically with your screen resolution")
                    }

                    Button {
                        text: i18n("Small")
                        flat: parent.selectedSize !== 1
                        highlighted: parent.selectedSize === 1
                        onClicked: {
                            sizeButtonRow.customModeActive = false;
                            root.writeSetting("SizeMode", 1, function (v) {
                                appSettings.zoneSelectorSizeMode = v;
                            });
                            root.writeSetting("PreviewWidth", root.constants.zoneSelectorPreviewSmall, function (v) {
                                appSettings.zoneSelectorPreviewWidth = v;
                            });
                            root.writeSetting("PreviewHeight", Math.round(root.constants.zoneSelectorPreviewSmall / root.safeAspectRatio), function (v) {
                                appSettings.zoneSelectorPreviewHeight = v;
                            });
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("%1px width", root.constants.zoneSelectorPreviewSmall)
                    }

                    Button {
                        text: i18n("Medium")
                        flat: parent.selectedSize !== 2
                        highlighted: parent.selectedSize === 2
                        onClicked: {
                            sizeButtonRow.customModeActive = false;
                            root.writeSetting("SizeMode", 1, function (v) {
                                appSettings.zoneSelectorSizeMode = v;
                            });
                            root.writeSetting("PreviewWidth", root.constants.zoneSelectorPreviewMedium, function (v) {
                                appSettings.zoneSelectorPreviewWidth = v;
                            });
                            root.writeSetting("PreviewHeight", Math.round(root.constants.zoneSelectorPreviewMedium / root.safeAspectRatio), function (v) {
                                appSettings.zoneSelectorPreviewHeight = v;
                            });
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("%1px width", root.constants.zoneSelectorPreviewMedium)
                    }

                    Button {
                        text: i18n("Large")
                        flat: parent.selectedSize !== 3
                        highlighted: parent.selectedSize === 3
                        onClicked: {
                            sizeButtonRow.customModeActive = false;
                            root.writeSetting("SizeMode", 1, function (v) {
                                appSettings.zoneSelectorSizeMode = v;
                            });
                            root.writeSetting("PreviewWidth", root.constants.zoneSelectorPreviewLarge, function (v) {
                                appSettings.zoneSelectorPreviewWidth = v;
                            });
                            root.writeSetting("PreviewHeight", Math.round(root.constants.zoneSelectorPreviewLarge / root.safeAspectRatio), function (v) {
                                appSettings.zoneSelectorPreviewHeight = v;
                            });
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("%1px width", root.constants.zoneSelectorPreviewLarge)
                    }

                    Button {
                        text: i18n("Custom")
                        flat: parent.selectedSize !== 4
                        highlighted: parent.selectedSize === 4
                        onClicked: {
                            sizeButtonRow.customModeActive = true;
                            root.writeSetting("SizeMode", 1, function (v) {
                                appSettings.zoneSelectorSizeMode = v;
                            });
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: Kirigami.Units.toolTipDelay
                        ToolTip.text: i18n("Custom size with slider")
                    }
                }

                // Custom size slider - only visible when Custom is selected
                RowLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    Layout.maximumWidth: Kirigami.Units.gridUnit * 25
                    visible: sizeButtonRow.selectedSize === 4
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Size:")
                    }

                    Slider {
                        id: customSizeSlider

                        Layout.fillWidth: true
                        from: root.constants.zoneSelectorPreviewWidthMin
                        to: root.constants.zoneSelectorPreviewWidthMax
                        stepSize: 10
                        Accessible.name: i18n("Preview size")
                        onMoved: {
                            root.writeSetting("PreviewWidth", value, function (v) {
                                appSettings.zoneSelectorPreviewWidth = v;
                            });
                            // Always maintain aspect ratio
                            var newHeight = Math.round(value / root.safeAspectRatio);
                            newHeight = Math.max(root.constants.zoneSelectorPreviewHeightMin, Math.min(root.constants.zoneSelectorPreviewHeightMax, newHeight));
                            root.writeSetting("PreviewHeight", newHeight, function (v) {
                                appSettings.zoneSelectorPreviewHeight = v;
                            });
                        }

                        Binding on value {
                            value: root.effectivePreviewWidth
                            when: !customSizeSlider.pressed
                            restoreMode: Binding.RestoreNone
                        }
                    }

                    Label {
                        text: root.effectivePreviewWidth + " px"
                        Layout.preferredWidth: root.constants.sliderValueLabelWidth + Kirigami.Units.largeSpacing
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }
                }

                // Info text for auto mode
                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.effectiveSizeMode === 0
                    text: i18n("Preview size adjusts automatically based on your screen resolution.")
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
