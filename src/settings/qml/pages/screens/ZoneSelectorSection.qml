// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Zone selector popup settings section with per-monitor overrides
 *
 * Composition host for the SNAPPING selector variant: info message, enable
 * toggle, and the three per-monitor cards. The Position and Preview Size
 * cards are the shared components (ZoneSelectorPositionCard /
 * ZoneSelectorPreviewSizeCard) also used by ScrollingZoneSelectorSection;
 * the Layout Arrangement card is snapping-only (the strip popup is a single
 * horizontal card row by construction) and stays inline. Per-monitor
 * overrides are scoped by each card's header scope chip (shared app-wide
 * scope, resting on "All Monitors").
 */
ColumnLayout {
    id: root

    required property var appSettings
    // Controller exposing the per-screen / scope methods (scopeScreenName,
    // hasPerScreenZoneSelectorSettings, clearPerScreenZoneSelectorSettings).
    // Required: the raw ISettings object passed as `appSettings` does NOT carry
    // the scope chip's Q_INVOKABLEs, so the two must be supplied independently.
    required property var controller
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

    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    // Write a preview width plus its aspect-derived height, clamping the
    // height to its min/max bounds. Shared by the Small/Medium/Large preset
    // buttons and the custom size slider so portrait monitors (aspect ratio
    // below 1) cannot push the stored height past its maximum.
    function _writePreviewSize(presetWidth) {
        writeSetting("PreviewWidth", presetWidth, function (v) {
            appSettings.zoneSelectorPreviewWidth = v;
        });
        var newHeight = Math.round(presetWidth / safeAspectRatio);
        newHeight = Math.max(constants.zoneSelectorPreviewHeightMin, Math.min(constants.zoneSelectorPreviewHeightMax, newHeight));
        writeSetting("PreviewHeight", newHeight, function (v) {
            appSettings.zoneSelectorPreviewHeight = v;
        });
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

    ZoneSelectorPositionCard {
        controller: root.controller
        constants: root.constants
        featureEnabled: root.appSettings.zoneSelectorEnabled
        effectivePosition: root.effectivePosition
        effectiveTriggerDistance: root.effectiveTriggerDistance
        scopeHasOverridesMethod: "hasPerScreenZoneSelectorSettings"
        scopeClearerMethod: "clearPerScreenZoneSelectorSettings"
        writePosition: function (v) {
            root.writeSetting("Position", v, function (gv) {
                root.appSettings.zoneSelectorPosition = gv;
            });
        }
        writeTriggerDistance: function (v) {
            root.writeSetting("TriggerDistance", v, function (gv) {
                root.appSettings.zoneSelectorTriggerDistance = gv;
            });
        }
    }

    // Layout Arrangement card — snapping-only (grid/vertical arrangements
    // have no meaning for the strip popup's single card row).
    Item {
        Layout.fillWidth: true
        implicitHeight: layoutCard.implicitHeight

        SettingsCard {
            id: layoutCard

            anchors.fill: parent
            enabled: appSettings.zoneSelectorEnabled
            headerText: i18n("Layout arrangement")
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
                        model: settingsController.valueOptions("Snapping.ZoneSelector", "LayoutMode")
                        storedValue: root.effectiveLayoutMode
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
                        id: gridColumnsSpin

                        accessibleName: i18n("Grid columns")
                        from: root.constants.zoneSelectorGridColumnsMin
                        to: root.constants.zoneSelectorGridColumnsMax
                        unitText: ""
                        onValueModified: value => {
                            return root.writeSetting("GridColumns", value, function (v) {
                                appSettings.zoneSelectorGridColumns = v;
                            });
                        }
                        // Feed value through a guarded Binding so scope switches and
                        // override clears keep refreshing the control: a plain `value:`
                        // binding is destroyed by SettingsSpinBox's own edit echo after
                        // the first edit. RestoreNone + the focus gate keeps a live
                        // edit from being clobbered.
                        Binding on value {
                            value: root.effectiveGridColumns
                            when: !gridColumnsSpin.editing
                            restoreMode: Binding.RestoreNone
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
                        id: maxRowsSpin

                        accessibleName: i18n("Max visible rows")
                        from: root.constants.zoneSelectorMaxRowsMin
                        to: root.constants.zoneSelectorMaxRowsMax
                        unitText: ""
                        onValueModified: value => {
                            return root.writeSetting("MaxRows", value, function (v) {
                                appSettings.zoneSelectorMaxRows = v;
                            });
                        }
                        // See gridColumnsSpin: guarded Binding so scope switches keep
                        // refreshing after the first edit destroys a plain binding.
                        Binding on value {
                            value: root.effectiveMaxRows
                            when: !maxRowsSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }
    }

    ZoneSelectorPreviewSizeCard {
        controller: root.controller
        constants: root.constants
        featureEnabled: root.appSettings.zoneSelectorEnabled
        safeAspectRatio: root.safeAspectRatio
        effectiveSizeMode: root.effectiveSizeMode
        effectivePreviewWidth: root.effectivePreviewWidth
        effectivePreviewHeight: root.effectivePreviewHeight
        scopeHelper: psHelper
        scopeHasOverridesMethod: "hasPerScreenZoneSelectorSettings"
        scopeClearerMethod: "clearPerScreenZoneSelectorSettings"
        writeSizeMode: function (v) {
            root.writeSetting("SizeMode", v, function (gv) {
                root.appSettings.zoneSelectorSizeMode = gv;
            });
        }
        writePreviewSize: root._writePreviewSize
    }
}
