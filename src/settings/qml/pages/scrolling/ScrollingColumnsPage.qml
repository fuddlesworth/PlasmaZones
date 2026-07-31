// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Columns: what a fresh column and a fresh tile look
 * like (default width and height, display mode, tab indicator) and the
 * preset lists the cycle shortcuts step through. One of the three advanced
 * scrolling leaves (View / Columns / Window).
 *
 * The New columns card is per-monitor overridable through its scope chip
 * (the Columns sub-domain of the per-screen scrolling map); the tab
 * indicator row and the preset lists are app-wide.
 */
SettingsFlickable {
    id: root

    // Width kind selector values, mirroring ConfigDefaults'
    // scrollingDefaultColumnWidthKind vocabulary and value bounds, read
    // once from ConfigDefaults via the controller — the C++ side is the
    // single home for these numbers (kind ints, slider range, spin range).
    readonly property var _scrollWidthConsts: settingsController.scrollingWidthConstants()
    readonly property int widthKindProportion: _scrollWidthConsts.kindProportion
    readonly property int widthKindFixed: _scrollWidthConsts.kindFixed
    readonly property int widthKindClientDecides: _scrollWidthConsts.kindClientDecides
    readonly property int widthKindPreset: _scrollWidthConsts.kindPreset
    readonly property int heightKindFixed: _scrollWidthConsts.heightKindFixed
    readonly property int heightKindPreset: _scrollWidthConsts.heightKindPreset

    // Largest legal preset index for the CURRENT lists (the schema caps the
    // stored index independently; the engine clamps at relayout).
    readonly property int widthPresetCount: appSettings.scrollingPresetColumnWidths.split(",").length
    readonly property int heightPresetCount: appSettings.scrollingPresetWindowHeights.split(",").length

    // Per-monitor override plumbing, the tiling Algorithm card's pattern:
    // rows read through settingValue (override wins over the global) and
    // write through writeSetting (routes to the per-screen setter when a
    // monitor is scoped).
    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenScrollingSettings"
        setterMethod: "setPerScreenScrollingSetting"
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // New Columns Card
        // =================================================================
        SettingsCard {
            id: newColumnsCard

            Layout.fillWidth: true
            headerText: i18n("New columns")
            searchAnchor: "newColumns"
            collapsible: true
            // The one per-monitor scrolling card, the analogue of the Tiling
            // Algorithm card: default column/tile sizing is layout tuning a
            // monitor legitimately owns. Everything else scrolling offers is
            // app-wide, with rules as the per-context escape hatch. The map
            // holds only this card's keys, so the whole-domain accessors
            // serve as its sub-domain.
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenScrollingSettings"
            scopeClearerMethod: "clearPerScreenScrollingSettings"

            // The kind the visible rows key off: the scoped monitor's
            // override when present, else the global.
            readonly property int effectiveWidthKind: root.settingValue("DefaultColumnWidthKind", appSettings.scrollingDefaultColumnWidthKind)
            readonly property int effectiveHeightKind: root.settingValue("DefaultWindowHeightKind", appSettings.scrollingDefaultWindowHeightKind)

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Default width")
                    searchAnchor: "defaultColumnWidthKind"
                    description: i18n("How wide a column is when it first opens")

                    WideComboBox {
                        Accessible.name: i18n("Default column width")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "DefaultColumnWidthKind")
                        storedValue: newColumnsCard.effectiveWidthKind
                        onActivated: root.writeSetting("DefaultColumnWidthKind", currentValue, function (v) {
                            appSettings.scrollingDefaultColumnWidthKind = v;
                        })
                    }
                }

                SettingsSeparator {
                    visible: newColumnsCard.effectiveWidthKind !== root.widthKindClientDecides
                }

                // Proportion and pixel widths share one stored value, so only
                // the control matching the selected kind is shown. A disabled
                // SettingsRow collapses out of the layout, which is also what
                // hides both rows for "window decides".
                SettingsRow {
                    title: i18n("Proportion of the screen")
                    searchAnchor: "defaultColumnWidthProportion"
                    description: i18n("How much of the usable screen width a new column takes")
                    enabled: newColumnsCard.effectiveWidthKind === root.widthKindProportion

                    SettingsSlider {
                        accessibleName: i18n("Proportion of the screen")
                        from: root._scrollWidthConsts.proportionMin
                        to: root._scrollWidthConsts.proportionMax
                        stepSize: root._scrollWidthConsts.proportionStep
                        value: root.settingValue("DefaultColumnWidthValue", appSettings.scrollingDefaultColumnWidthValue)
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: function (newValue) {
                            root.writeSetting("DefaultColumnWidthValue", newValue, function (v) {
                                appSettings.scrollingDefaultColumnWidthValue = v;
                            });
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Fixed width")
                    searchAnchor: "defaultColumnWidthFixed"
                    description: i18n("How many pixels wide a new column is")
                    enabled: newColumnsCard.effectiveWidthKind === root.widthKindFixed

                    SettingsSpinBox {
                        id: fixedWidthSpin

                        accessibleName: i18n("Fixed column width")
                        from: root._scrollWidthConsts.fixedMin
                        to: root._scrollWidthConsts.fixedMax
                        stepSize: root._scrollWidthConsts.fixedStep
                        onValueModified: value => root.writeSetting("DefaultColumnWidthValue", value, function (v) {
                                appSettings.scrollingDefaultColumnWidthValue = v;
                            })
                        // Fed through a guarded Binding rather than a plain
                        // `value:` one: SettingsSpinBox echoes each edit back
                        // into its own `value`, which would destroy a plain
                        // binding after the first edit and strand every later
                        // config-side change.
                        Binding on value {
                            value: Math.round(root.settingValue("DefaultColumnWidthValue", appSettings.scrollingDefaultColumnWidthValue))
                            when: !fixedWidthSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Preset width")
                    searchAnchor: "defaultColumnWidthPresetIndex"
                    description: i18n("Which entry of the column width presets a new column opens at, counted from 1. Columns opened this way follow later preset changes.")
                    enabled: newColumnsCard.effectiveWidthKind === root.widthKindPreset

                    SettingsSpinBox {
                        id: widthPresetIndexSpin

                        accessibleName: i18n("Column width preset number")
                        from: 1
                        to: Math.max(1, root.widthPresetCount)
                        stepSize: 1
                        // Stored 0-based, shown 1-based to match the preset
                        // cycling OSD.
                        onValueModified: value => root.writeSetting("DefaultColumnWidthPresetIndex", value - 1, function (v) {
                                appSettings.scrollingDefaultColumnWidthPresetIndex = v;
                            })
                        Binding on value {
                            value: root.settingValue("DefaultColumnWidthPresetIndex", appSettings.scrollingDefaultColumnWidthPresetIndex) + 1
                            when: !widthPresetIndexSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Open new columns as")
                    searchAnchor: "defaultColumnDisplay"
                    description: i18n("Normal stacks the windows of a column above each other. Tabbed shows one window at a time behind a tab strip.")

                    WideComboBox {
                        Accessible.name: i18n("Open new columns as")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "DefaultColumnDisplay")
                        storedValue: root.settingValue("DefaultColumnDisplay", appSettings.scrollingDefaultColumnDisplay)
                        onActivated: root.writeSetting("DefaultColumnDisplay", currentValue, function (v) {
                            appSettings.scrollingDefaultColumnDisplay = v;
                        })
                    }
                }

                SettingsSeparator {}

                // App-wide row on a scoped card (like the Window Handling
                // card's global rows): the indicator is one overlay service.
                SettingsRow {
                    title: i18n("Tab indicator")
                    searchAnchor: "tabStripEnabled"
                    description: i18n("Show a pill of tabs above a tabbed column. Tabbed columns keep working without it.")

                    SettingsSwitch {
                        checked: appSettings.scrollingTabStripEnabled
                        accessibleName: i18n("Tab indicator")
                        onToggled: function (newValue) {
                            appSettings.scrollingTabStripEnabled = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Default height")
                    searchAnchor: "defaultWindowHeightKind"
                    description: i18n("How tall a window is when it joins a column. Share the column evenly splits the remaining space with its neighbors.")

                    WideComboBox {
                        Accessible.name: i18n("Default window height")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "DefaultWindowHeightKind")
                        storedValue: newColumnsCard.effectiveHeightKind
                        onActivated: root.writeSetting("DefaultWindowHeightKind", currentValue, function (v) {
                            appSettings.scrollingDefaultWindowHeightKind = v;
                        })
                    }
                }

                SettingsRow {
                    title: i18n("Fixed height")
                    searchAnchor: "defaultWindowHeightFixed"
                    description: i18n("How many pixels tall a new window is")
                    enabled: newColumnsCard.effectiveHeightKind === root.heightKindFixed

                    SettingsSpinBox {
                        id: fixedHeightSpin

                        accessibleName: i18n("Fixed window height")
                        from: root._scrollWidthConsts.heightFixedMin
                        to: root._scrollWidthConsts.heightFixedMax
                        stepSize: root._scrollWidthConsts.fixedStep
                        onValueModified: value => root.writeSetting("DefaultWindowHeightValue", value, function (v) {
                                appSettings.scrollingDefaultWindowHeightValue = v;
                            })
                        // Same guarded-binding rationale as the fixed width.
                        Binding on value {
                            value: Math.round(root.settingValue("DefaultWindowHeightValue", appSettings.scrollingDefaultWindowHeightValue))
                            when: !fixedHeightSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Preset height")
                    searchAnchor: "defaultWindowHeightPresetIndex"
                    description: i18n("Which entry of the window height presets a new window opens at, counted from 1")
                    enabled: newColumnsCard.effectiveHeightKind === root.heightKindPreset

                    SettingsSpinBox {
                        id: heightPresetIndexSpin

                        accessibleName: i18n("Window height preset number")
                        from: 1
                        to: Math.max(1, root.heightPresetCount)
                        stepSize: 1
                        // Stored 0-based, shown 1-based (see the width twin).
                        onValueModified: value => root.writeSetting("DefaultWindowHeightPresetIndex", value - 1, function (v) {
                                appSettings.scrollingDefaultWindowHeightPresetIndex = v;
                            })
                        Binding on value {
                            value: root.settingValue("DefaultWindowHeightPresetIndex", appSettings.scrollingDefaultWindowHeightPresetIndex) + 1
                            when: !heightPresetIndexSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }

        // =================================================================
        // Presets Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Width and height presets")
            searchAnchor: "scrollingPresets"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Column widths")
                    searchAnchor: "presetColumnWidths"
                    description: i18n("Percentages of the work area width, cycled in this order by the preset shortcuts")
                }

                PresetListEditor {
                    Layout.fillWidth: true
                    presets: appSettings.scrollingPresetColumnWidths
                    entryName: i18n("column width preset")
                    commit: function (joined) {
                        appSettings.scrollingPresetColumnWidths = joined;
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Window heights")
                    searchAnchor: "presetWindowHeights"
                    description: i18n("Percentages of the work area height, cycled in this order by the preset shortcuts")
                }

                PresetListEditor {
                    Layout.fillWidth: true
                    presets: appSettings.scrollingPresetWindowHeights
                    entryName: i18n("window height preset")
                    commit: function (joined) {
                        appSettings.scrollingPresetWindowHeights = joined;
                    }
                }
            }
        }
    }
}
