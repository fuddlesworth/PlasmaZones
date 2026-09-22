// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "../../js/PresetList.js" as PresetList

/**
 * @brief Simple-mode Scrolling page: the everyday decisions (default column
 * width, and whether tabbed columns are marked and how) plus three shared
 * cards — Strip direction, Window Handling, and Focus and view. Which way the
 * strip runs is the Strip direction card's own row; the Focus card carries
 * the centering and wheel rows. The advanced counterpart is the Columns page
 * (scrolling-columns); dirtiness, Reset, and Discard delegate to all three
 * advanced leaves via simplePageBackingPages.
 *
 * Scope: every row this page OWNS binds appSettings directly, like
 * TilingSimplePage. The one deliberate exception is the shared Strip
 * direction card, which carries its per-monitor scope chip on both hosting
 * pages — a portrait monitor is exactly the everyday case the card exists
 * for, so simple mode edits the per-monitor axis in place. Column SIZING
 * overrides stay advanced-mode depth, so for those this page only warns that
 * one is in effect rather than offering to edit it.
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

    // Largest 1-based preset number the spin may offer: the shorter of the
    // live list and the schema's stored-index ceiling. Same rule as the
    // Columns page's presetSpinCeiling, over the same global key.
    function presetSpinCeiling(count) {
        return Math.max(1, Math.min(count, root._scrollConsts.presetIndexMax + 1));
    }

    // Gate on construction being finished: the count binding settles while
    // the page is built, and a write from that first evaluation would badge
    // the page dirty before the user has touched anything.
    property bool built: false

    Component.onCompleted: root.built = true

    // Twin of ScrollingColumnsPage's write-back over the same global key: a
    // preset list can shrink under a stored index, and the spin would then
    // display-clamp without firing onValueModified, leaving config holding an
    // index the user can no longer see. This page is global-scope only, so
    // the write goes straight to appSettings.
    onPresetCountChanged: {
        if (!root.built)
            return;
        var maxIndex = root.presetSpinCeiling(root.presetCount) - 1;
        if (appSettings.scrollingDefaultColumnWidthPresetIndex > maxIndex)
            appSettings.scrollingDefaultColumnWidthPresetIndex = maxIndex;
    }

    // Bumped on every override change so the check below re-runs:
    // hasPerScreenScrollingSizingSettings is a plain invokable with no NOTIFY,
    // so a binding over it would never re-evaluate on its own.
    property int overrideRevision: 0

    // True while any monitor carries a column-SIZING override, which would
    // win over the sizing values this page edits. Deliberately not widened to
    // the axis sub-domain: the Strip direction card hosted below carries its
    // own per-monitor chip (with an override dot), so an axis override is
    // visible and editable in place rather than warned about.
    readonly property bool anyScreenOverridden: {
        void root.overrideRevision;
        var list = settingsController.screens || [];
        for (var i = 0; i < list.length; i++) {
            if (settingsController.hasPerScreenScrollingSizingSettings(list[i].name))
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
                    title: i18n("Proportion of the strip")
                    searchAnchor: "simpleDefaultColumnWidthProportion"
                    description: i18n("How much of the strip a new column takes")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindProportion
                    visible: true

                    SettingsSlider {
                        accessibleName: i18n("Proportion of the strip")
                        // Percent units, not the stored fraction: the step is
                        // 1% and the tick-mark hint is an int, so ticks can
                        // only stay readable (one per 5%) in whole percent.
                        from: Math.round(root._scrollConsts.proportionMin * 100)
                        to: Math.round(root._scrollConsts.proportionMax * 100)
                        stepSize: Math.max(1, Math.round(root._scrollConsts.proportionStep * 100))
                        tickMarkStepSize: 5
                        value: appSettings.scrollingDefaultColumnWidthValue * 100
                        formatValue: function (v) {
                            return Math.round(v) + "%";
                        }
                        onMoved: function (newValue) {
                            appSettings.scrollingDefaultColumnWidthValue = Math.round(newValue) / 100;
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Fixed width")
                    searchAnchor: "simpleDefaultColumnWidthFixed"
                    description: i18n("How many pixels a new column takes along the strip")
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
                    description: i18n("Which width a new column opens at, counted from 1 into the widths of the screen's layout template. Screens with no template of their own use the default template from Scrolling → Templates, and with no template at all the built-in width steps apply.")
                    enabled: appSettings.scrollingDefaultColumnWidthKind === root.widthKindPreset
                    visible: true

                    SettingsSpinBox {
                        id: presetIndexSpin

                        accessibleName: i18n("Column width preset number")
                        // An ordinal, not a measurement.
                        unitText: ""
                        from: 1
                        to: root.presetSpinCeiling(root.presetCount)
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

        // =================================================================
        // Tabs Card
        // =================================================================
        // Three of the eighteen tab-indicator knobs: whether it shows, and
        // the two that change what you actually see. The rest (thickness,
        // length, the two gaps, corner radius, colours, the five label-font
        // knobs, place-within-column, hide-for-single-tab) are tuning for an
        // indicator you have already decided you want, which is advanced-mode
        // depth — they live on Scrolling → Tabs.
        //
        // These keys are OWNED by scrolling-tabs, so this page has no
        // pageOwnedConfigKeys entry for them; dirtiness, Reset and Discard
        // delegate through simplePageBackingPages, which lists scrolling-tabs
        // for exactly this reason.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Tabs")
            searchAnchor: "simpleTabs"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Show the tab indicator")
                    searchAnchor: "simpleTabIndicatorEnabled"
                    description: i18n("Mark a tabbed column's windows on screen. Tabbed columns keep working without it.")

                    SettingsSwitch {
                        checked: appSettings.scrollingTabIndicatorEnabled
                        accessibleName: i18n("Show the tab indicator")
                        onToggled: function (newValue) {
                            appSettings.scrollingTabIndicatorEnabled = newValue;
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Style")
                    searchAnchor: "simpleTabIndicatorStyle"
                    description: i18n("Titled chips label each window. A segment bar is thinner and shows only how many there are.")
                    enabled: appSettings.scrollingTabIndicatorEnabled

                    WideComboBox {
                        Accessible.name: i18n("Tab indicator style")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling.TabIndicator", "Style")
                        storedValue: appSettings.scrollingTabIndicatorStyle
                        onActivated: appSettings.scrollingTabIndicatorStyle = currentValue
                    }
                }

                SettingsRow {
                    title: i18n("Position")
                    searchAnchor: "simpleTabIndicatorPosition"
                    description: i18n("Which edge of the column the indicator runs along.")
                    enabled: appSettings.scrollingTabIndicatorEnabled

                    WideComboBox {
                        Accessible.name: i18n("Tab indicator position")
                        textRole: "text"
                        valueRole: "value"
                        model: settingsController.valueOptions("Scrolling.TabIndicator", "Position")
                        storedValue: appSettings.scrollingTabIndicatorPosition
                        onActivated: appSettings.scrollingTabIndicatorPosition = currentValue
                    }
                }
            }
        }

        ScrollingWindowHandlingCard {
            Layout.fillWidth: true
        }

        ScrollingStripDirectionCard {
            Layout.fillWidth: true
        }

        ScrollingFocusCard {
            Layout.fillWidth: true
        }
    }
}
