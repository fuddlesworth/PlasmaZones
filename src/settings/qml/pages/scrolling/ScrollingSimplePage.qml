// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../../js/PresetList.js" as PresetList

/**
 * @brief Simple-mode Scrolling page: the everyday decision (default column
 * width) plus the shared Window Handling and Focus and view cards, the latter
 * carrying the centering and wheel rows. The advanced counterpart is the
 * Columns page (scrolling-columns); dirtiness, Reset, and Discard delegate to
 * both advanced leaves via simplePageBackingPages.
 *
 * Global scope only, like TilingSimplePage: every row binds appSettings
 * directly and no per-monitor scope chip is offered here. Per-monitor
 * overrides are advanced-mode depth, so this page only warns that one is in
 * effect rather than offering to edit it.
 *
 * The width-kind combo offers all four kinds, so this page hosts a control
 * for each one it can host. Restricting the combo instead would leave a
 * stored Fixed or Preset width unreadable in simple mode.
 */
SettingsFlickable {
    id: root

    readonly property var _scrollConsts: settingsController.scrollingConstants()
    readonly property int widthKindProportion: _scrollConsts.kindProportion
    readonly property int widthKindFixed: _scrollConsts.kindFixed
    readonly property int widthKindPreset: _scrollConsts.kindPreset

    readonly property int presetCount: PresetList.count(appSettings.scrollingPresetColumnWidths)

    // Bumped on every override change so the check below re-runs:
    // hasPerScreenScrollingSettings is a plain invokable with no NOTIFY, so a
    // binding over it would never re-evaluate on its own.
    property int overrideRevision: 0

    // True while any monitor carries a scrolling override, which would win
    // over the global values this page edits.
    readonly property bool anyScreenOverridden: {
        void root.overrideRevision;
        var list = settingsController.screens || [];
        for (var i = 0; i < list.length; i++) {
            if (settingsController.hasPerScreenScrollingSettings(list[i].name))
                return true;
        }
        return false;
    }

    contentHeight: content.implicitHeight
    clip: true

    // Recompute the override notice when an override is added or cleared on
    // another page.
    Connections {
        function onPerScreenOverridesChanged() {
            root.overrideRevision++;
        }

        target: settingsController
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: root.anyScreenOverridden
            text: i18n("At least one monitor has its own column sizing, which wins over the values below. Switch to advanced mode to edit it on Scrolling → Columns.")
        }

        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Scrolling")
            searchAnchor: "scrollingSimple"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Default width")
                    searchAnchor: "simpleDefaultColumnWidthKind"
                    description: i18n("How wide a column is when it first opens")

                    WideComboBox {
                        Accessible.name: i18n("Default column width")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling", "DefaultColumnWidthKind")
                        storedValue: appSettings.scrollingDefaultColumnWidthKind
                        onActivated: appSettings.scrollingDefaultColumnWidthKind = currentValue
                    }
                }

                // The three width controls share one stored value, so only the
                // one matching the selected kind is live. They stay VISIBLE
                // while disabled rather than taking SettingsRow's default
                // collapse: each carries a search anchor, and a deep link that
                // reveals nothing is worse than a greyed row. A dependent row
                // hugs the row that gates it, so no separator sits between the
                // kind combo and the controls it governs.
                SettingsRow {
                    title: i18n("Proportion of the screen")
                    searchAnchor: "simpleDefaultColumnWidthProportion"
                    description: i18n("How much of the usable screen width a new column takes")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindProportion
                    visible: true

                    SettingsSlider {
                        accessibleName: i18n("Proportion of the screen")
                        from: root._scrollConsts.proportionMin
                        to: root._scrollConsts.proportionMax
                        stepSize: root._scrollConsts.proportionStep
                        value: appSettings.scrollingDefaultColumnWidthValue
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: function (newValue) {
                            appSettings.scrollingDefaultColumnWidthValue = newValue;
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Fixed width")
                    searchAnchor: "simpleDefaultColumnWidthFixed"
                    description: i18n("How many pixels wide a new column is")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindFixed
                    visible: true

                    SettingsSpinBox {
                        id: fixedWidthSpin

                        accessibleName: i18n("Fixed column width")
                        from: root._scrollConsts.fixedMin
                        to: root._scrollConsts.fixedMax
                        stepSize: root._scrollConsts.fixedStep
                        onValueModified: value => appSettings.scrollingDefaultColumnWidthValue = value
                        // Guarded Binding, not a plain `value:` one:
                        // SettingsSpinBox echoes each edit back into its own
                        // `value`, which would destroy a plain binding after
                        // the first edit.
                        Binding on value {
                            value: Math.round(appSettings.scrollingDefaultColumnWidthValue)
                            when: !fixedWidthSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Preset width")
                    searchAnchor: "simpleDefaultColumnWidthPresetIndex"
                    description: i18n("Which entry of the column width presets a new column opens at, counted from 1. The presets themselves are edited on the Columns page in advanced mode.")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindPreset
                    visible: true

                    SettingsSpinBox {
                        id: presetIndexSpin

                        accessibleName: i18n("Column width preset number")
                        // An ordinal, not a measurement.
                        unitText: ""
                        from: 1
                        to: Math.max(1, Math.min(root.presetCount, root._scrollConsts.presetIndexMax + 1))
                        stepSize: 1
                        // Stored 0-based, shown 1-based to match the preset
                        // cycling OSD.
                        onValueModified: value => appSettings.scrollingDefaultColumnWidthPresetIndex = value - 1
                        Binding on value {
                            value: appSettings.scrollingDefaultColumnWidthPresetIndex + 1
                            when: !presetIndexSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }

        ScrollingWindowHandlingCard {
            Layout.fillWidth: true
        }

        ScrollingFocusCard {
            Layout.fillWidth: true
        }
    }
}
