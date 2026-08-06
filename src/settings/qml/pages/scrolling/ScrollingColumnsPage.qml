// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../../js/PresetList.js" as PresetList

/**
 * @brief Scrolling → Columns: what a fresh column and a fresh tile look like
 * (default width and height, display mode), plus the template a scrolling
 * screen falls back to when it has none assigned. One of the three advanced
 * scrolling leaves (Columns / Tabs / Window).
 *
 * The New columns card is per-monitor overridable through its scope chip
 * (the Columns sub-domain of the per-screen scrolling map). The preset
 * vocabularies the cycling shortcuts step through live on templates now, so
 * this page only picks the default template and leaves editing them to the
 * Layouts page.
 *
 * This page decides WHICH columns open tabbed (the display row on the New
 * columns card). How a tabbed column's indicator is drawn is the Tabs leaf.
 */
SettingsFlickable {
    id: root

    // Kind vocabularies and value bounds, mirroring ConfigDefaults and read
    // once through the controller — the C++ side is the single home for these
    // numbers (kind ints, slider and spin ranges, the preset-index ceiling).
    readonly property var _scrollConsts: settingsController.scrollingConstants()
    readonly property int widthKindProportion: _scrollConsts.kindProportion
    readonly property int widthKindFixed: _scrollConsts.kindFixed
    readonly property int widthKindPreset: _scrollConsts.kindPreset
    readonly property int heightKindFixed: _scrollConsts.heightKindFixed
    readonly property int heightKindPreset: _scrollConsts.heightKindPreset

    // Live preset counts, through the shared PresetList.js parse
    // so the page has exactly one implementation of "how many presets".
    readonly property int widthPresetCount: PresetList.count(appSettings.scrollingPresetColumnWidths)
    readonly property int heightPresetCount: PresetList.count(appSettings.scrollingPresetWindowHeights)

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

    // Largest 1-based preset number a spin may offer: the shorter of the live
    // list and the schema's stored-index ceiling.
    function presetSpinCeiling(count) {
        return Math.max(1, Math.min(count, root._scrollConsts.presetIndexMax + 1));
    }

    // Gate on construction being finished: the count bindings settle while the
    // page is built, and a write from that first evaluation would badge the
    // page dirty before the user has touched anything.
    property bool built: false

    Component.onCompleted: root.built = true

    // A preset list can shrink under a stored index. The spin would then
    // display-clamp without firing onValueModified, leaving config holding an
    // index the user can no longer see, so write the clamped value back.
    function clampPresetIndex(count, globalValue, globalSetter) {
        if (!root.built)
            return;
        var maxIndex = root.presetSpinCeiling(count) - 1;
        // Clamp the GLOBAL value through the global setter directly. Routing
        // through writeSetting while a monitor happens to be scoped would
        // materialize a stray per-screen override holding the clamp and leave
        // the global index unclamped. A scoped override past the ceiling
        // display-clamps in the spin and re-clamps at relayout, so it needs
        // no write-back here.
        if (globalValue > maxIndex)
            globalSetter(maxIndex);
    }

    onWidthPresetCountChanged: root.clampPresetIndex(root.widthPresetCount, appSettings.scrollingDefaultColumnWidthPresetIndex, function (v) {
        appSettings.scrollingDefaultColumnWidthPresetIndex = v;
    })
    onHeightPresetCountChanged: root.clampPresetIndex(root.heightPresetCount, appSettings.scrollingDefaultWindowHeightPresetIndex, function (v) {
        appSettings.scrollingDefaultWindowHeightPresetIndex = v;
    })

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

            // The kind the size rows key off: the scoped monitor's override
            // when present, else the global.
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

                // The three width controls share one stored value, so only the
                // one matching the selected kind is live. They stay VISIBLE
                // while disabled rather than taking SettingsRow's default
                // collapse: each carries a search anchor, and a deep link that
                // reveals nothing is worse than a greyed row that shows which
                // kind owns it. Separator convention on this page: a dependent
                // row hugs the row that gates it, so no separator sits between
                // a kind combo and the controls it governs.
                SettingsRow {
                    title: i18n("Proportion of the screen")
                    searchAnchor: "defaultColumnWidthProportion"
                    description: i18n("How much of the usable screen width a new column takes")
                    enabled: newColumnsCard.effectiveWidthKind === root.widthKindProportion
                    visible: true

                    SettingsSlider {
                        accessibleName: i18n("Proportion of the screen")
                        from: root._scrollConsts.proportionMin
                        to: root._scrollConsts.proportionMax
                        stepSize: root._scrollConsts.proportionStep
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
                    visible: true

                    SettingsSpinBox {
                        id: fixedWidthSpin

                        accessibleName: i18n("Fixed column width")
                        from: root._scrollConsts.fixedMin
                        to: root._scrollConsts.fixedMax
                        stepSize: root._scrollConsts.fixedStep
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
                    description: i18n("Which entry of the column width presets a new column opens at, counted from 1. Columns opened this way follow later preset changes. On a screen with a layout template the number counts into the template's widths instead.")
                    enabled: newColumnsCard.effectiveWidthKind === root.widthKindPreset
                    visible: true

                    SettingsSpinBox {
                        id: widthPresetIndexSpin

                        accessibleName: i18n("Column width preset number")
                        // An ordinal, not a measurement — SettingsSpinBox's
                        // default unit would render it as pixels.
                        unitText: ""
                        from: 1
                        to: root.presetSpinCeiling(root.widthPresetCount)
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

                SettingsRow {
                    title: i18n("Default height")
                    searchAnchor: "defaultWindowHeightKind"
                    description: i18n("How tall a window is when it joins a column. With Share the column evenly, a new window splits the remaining space with its neighbors.")

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
                    visible: true

                    SettingsSpinBox {
                        id: fixedHeightSpin

                        accessibleName: i18n("Fixed window height")
                        from: root._scrollConsts.heightFixedMin
                        to: root._scrollConsts.heightFixedMax
                        stepSize: root._scrollConsts.heightFixedStep
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
                    description: i18n("Which entry of the window height presets a new window opens at, counted from 1. On a screen with a layout template the number counts into the template's heights instead.")
                    enabled: newColumnsCard.effectiveHeightKind === root.heightKindPreset
                    visible: true

                    SettingsSpinBox {
                        id: heightPresetIndexSpin

                        accessibleName: i18n("Window height preset number")
                        unitText: ""
                        from: 1
                        to: root.presetSpinCeiling(root.heightPresetCount)
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
        // Layout Template Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Layout template")
            searchAnchor: "scrollingDefaultTemplate"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                Label {
                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    Layout.rightMargin: Kirigami.Units.largeSpacing
                    text: i18n("Templates carry the starting columns and the width and height presets the cycling shortcuts step through. Manage them on the Layouts page under Scrolling Templates, and assign one per screen with the layout picker. The template chosen here applies to every scrolling screen without its own assignment.")
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.Wrap
                }

                SettingsRow {
                    title: i18n("Default template")
                    searchAnchor: "defaultScrollingTemplate"
                    description: i18n("Used when a scrolling screen has no template assigned")

                    ComboBox {
                        id: defaultTemplateCombo

                        // Templates from the shared layouts model (the
                        // isScrollingTemplate family), with a leading None.
                        function rebuild() {
                            let entries = [
                                {
                                    "text": i18n("None"),
                                    "value": ""
                                }
                            ];
                            const all = settingsController.layouts;
                            for (let i = 0; i < all.length; i++) {
                                if (all[i].isScrollingTemplate === true)
                                    entries.push({
                                        "text": all[i].displayName,
                                        "value": all[i].id
                                    });
                            }
                            model = entries;
                            // Re-resolve AFTER the model swap: indexOfValue
                            // inside a binding evaluates against the OLD
                            // model (the combo-derived reset trap).
                            // A miss stays at -1 rather than snapping to the
                            // leading None: config still holds the stored id,
                            // and displayText below names it as missing.
                            currentIndex = indexOfValue(appSettings.defaultScrollingTemplate);
                        }

                        textRole: "text"
                        valueRole: "value"
                        displayText: currentIndex >= 0 ? currentText : i18n("Missing template (%1)", appSettings.defaultScrollingTemplate)
                        Accessible.name: i18n("Default scrolling template")
                        onActivated: appSettings.defaultScrollingTemplate = currentValue
                        Component.onCompleted: rebuild()

                        Connections {
                            function onLayoutsChanged() {
                                defaultTemplateCombo.rebuild();
                            }

                            target: settingsController
                        }

                        Connections {
                            function onDefaultScrollingTemplateChanged() {
                                defaultTemplateCombo.currentIndex = defaultTemplateCombo.indexOfValue(appSettings.defaultScrollingTemplate);
                            }

                            target: appSettings
                        }
                    }
                }
            }
        }
    }
}
