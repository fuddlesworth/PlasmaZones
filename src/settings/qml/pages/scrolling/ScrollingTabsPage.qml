// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Tabs: the indicator drawn alongside a tabbed column.
 *
 * Its own leaf rather than a card on the Columns page: the family is thirteen
 * knobs across three concerns (whether it shows, how it is laid out, what
 * colour it is), which is a page's worth of surface, and none of it is
 * per-monitor overridable the way the Columns page's New columns card is.
 *
 * WHICH columns are tabbed is deliberately NOT here. That is the New columns
 * card on Scrolling → Columns (per-monitor overridable, unlike anything on
 * this page) and the Meta+Alt+T shortcut, which toggles the focused column.
 * This page only governs how a column that is already tabbed advertises it.
 *
 * The cards narrow the search, so a user hunting for one knob picks a card
 * rather than scanning the whole list. Layout is split across two of them
 * (Tab indicator carries style and position, Size and spacing carries the
 * measurements) because there are more measurements than anything else.
 */
SettingsFlickable {
    id: root

    // Slider bounds and steps, read once through the controller so the C++
    // side stays the single home for these numbers. The two enum vocabularies
    // do NOT come through here: the Style and Position combos take their
    // options from settingsController.valueOptions(), which carries the
    // labels along with the values.
    readonly property var _scrollConsts: settingsController.scrollingConstants()

    /// Every row below the master switch is dead while the indicator is off.
    /// Named once here rather than repeated per row so the gate cannot drift.
    readonly property bool indicatorOn: appSettings.scrollingTabIndicatorEnabled

    // PAGE-LEVEL, shared by the three colour rows: a page rebuild while the
    // dialog is open would destroy a row-scoped dialog and tear the popup down
    // under the user. (A card COLLAPSE would not — SettingsCard only drives
    // its clip height and never destroys the subtree.) Each row connects to `accepted`
    // transiently and disconnects on close, so sharing cannot cross wires.
    ColorDialog {
        id: tabColorDialog

        options: ColorDialog.ShowAlphaChannel
    }

    contentHeight: content.implicitHeight
    clip: true

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Tab Indicator Card
        // =================================================================
        // Whether the indicator shows at all, and what shape it takes. The
        // two visibility rows lead because everything after them is moot
        // when the indicator is off.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Tab indicator")
            searchAnchor: "tabIndicator"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Show the tab indicator")
                    searchAnchor: "tabIndicatorEnabled"
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
                    title: i18n("Hide it for a single window")
                    searchAnchor: "tabIndicatorHideWhenSingleTab"
                    description: i18n("Leave a tabbed column unmarked while it holds only one window.")
                    enabled: root.indicatorOn

                    SettingsSwitch {
                        checked: appSettings.scrollingTabIndicatorHideWhenSingleTab
                        accessibleName: i18n("Hide the tab indicator for a single window")
                        onToggled: function (newValue) {
                            appSettings.scrollingTabIndicatorHideWhenSingleTab = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Style")
                    searchAnchor: "tabIndicatorStyle"
                    description: i18n("Titled chips name each window. A segment bar is thinner and shows only how many there are.")
                    enabled: root.indicatorOn

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
                    searchAnchor: "tabIndicatorPosition"
                    description: i18n("Which edge of the column the indicator runs along.")
                    enabled: root.indicatorOn

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

        // =================================================================
        // Size and Spacing Card
        // =================================================================
        // Everything that decides how much room the indicator takes and
        // where that room comes from. Place-within-column leads: it is the
        // one row here that moves windows, and it changes what the gap below
        // it means.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Size and spacing")
            searchAnchor: "tabIndicatorSizing"
            collapsible: true
            enabled: root.indicatorOn

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Make room inside the column")
                    searchAnchor: "tabIndicatorPlaceWithinColumn"
                    description: i18n("Shrink the windows to fit the indicator. Off, it is drawn beside the column and can overlap a neighbor or run off screen.")

                    SettingsSwitch {
                        checked: appSettings.scrollingTabIndicatorPlaceWithinColumn
                        accessibleName: i18n("Make room for the tab indicator inside the column")
                        onToggled: function (newValue) {
                            appSettings.scrollingTabIndicatorPlaceWithinColumn = newValue;
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Gap")
                    searchAnchor: "tabIndicatorGap"
                    description: i18n("Space between the indicator and the window. A negative gap draws it over the window instead.")

                    SettingsSpinBox {
                        id: tabGapSpin

                        accessibleName: i18n("Gap around the tab indicator")
                        from: root._scrollConsts.tabGapMin
                        to: root._scrollConsts.tabGapMax
                        stepSize: 1
                        onValueModified: value => appSettings.scrollingTabIndicatorGap = value
                        // Guarded Binding, not a plain `value:` one: the control
                        // echoes each edit back into its own value, which would
                        // destroy a plain binding after the first edit and strand
                        // every later config-side change.
                        Binding on value {
                            value: appSettings.scrollingTabIndicatorGap
                            when: !tabGapSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Thickness")
                    searchAnchor: "tabIndicatorWidth"
                    description: i18n("How thick the indicator is. When it makes room inside the column, this is exactly how much room it takes. A segment bar reads well at a few pixels. Titled chips need enough for their titles, which on a left or right edge means a lot.")

                    SettingsSpinBox {
                        id: tabWidthSpin

                        accessibleName: i18n("Tab indicator thickness")
                        from: root._scrollConsts.tabWidthMin
                        to: root._scrollConsts.tabWidthMax
                        stepSize: 1
                        onValueModified: value => appSettings.scrollingTabIndicatorWidth = value
                        Binding on value {
                            value: appSettings.scrollingTabIndicatorWidth
                            when: !tabWidthSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Length")
                    searchAnchor: "tabIndicatorLength"
                    description: i18n("How much of the column edge the indicator spans, centered on it.")

                    SettingsSlider {
                        accessibleName: i18n("Tab indicator length")
                        from: root._scrollConsts.tabLengthMin
                        to: root._scrollConsts.tabLengthMax
                        stepSize: root._scrollConsts.tabLengthStep
                        value: appSettings.scrollingTabIndicatorLengthProportion
                        formatValue: function (v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: function (newValue) {
                            appSettings.scrollingTabIndicatorLengthProportion = newValue;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Gap between tabs")
                    searchAnchor: "tabIndicatorGapsBetweenTabs"
                    description: i18n("Space separating one tab from the next.")

                    SettingsSpinBox {
                        id: tabGapsBetweenSpin

                        accessibleName: i18n("Gap between tabs")
                        from: root._scrollConsts.tabGapsBetweenMin
                        to: root._scrollConsts.tabGapsBetweenMax
                        stepSize: 1
                        onValueModified: value => appSettings.scrollingTabIndicatorGapsBetweenTabs = value
                        Binding on value {
                            value: appSettings.scrollingTabIndicatorGapsBetweenTabs
                            when: !tabGapsBetweenSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }

                SettingsRow {
                    title: i18n("Fully rounded tabs")
                    searchAnchor: "tabIndicatorFullyRounded"
                    description: i18n("Round each tab to a pill. Off, the corner radius below applies instead.")

                    SettingsSwitch {
                        checked: appSettings.scrollingTabIndicatorCornerRadius === root._scrollConsts.tabCornerRadiusPill
                        accessibleName: i18n("Fully rounded tabs")
                        // The pill is stored as a sentinel in the same key the
                        // radius uses, so the toggle writes the sentinel one way
                        // and a plain radius the other. 0 (square) is the honest
                        // partner for "not a pill": any other seed would be this
                        // switch inventing a radius the user never chose.
                        onToggled: function (newValue) {
                            appSettings.scrollingTabIndicatorCornerRadius = newValue ? root._scrollConsts.tabCornerRadiusPill : 0;
                        }
                    }
                }

                // Dependent row, so it hugs the switch that gates it with no
                // separator between them — the convention the Columns page's
                // kind-and-value pairs follow.
                SettingsRow {
                    title: i18n("Corner radius")
                    searchAnchor: "tabIndicatorCornerRadius"
                    description: i18n("How rounded each tab's corners are. On a segment bar with no gap between tabs, only the two ends of the run are rounded.")
                    // Disabled, not hidden, while the pill sentinel is stored:
                    // a row that vanishes makes the switch above look like it
                    // deleted a setting, and the row carries a search anchor a
                    // deep link has to be able to reveal.
                    enabled: appSettings.scrollingTabIndicatorCornerRadius !== root._scrollConsts.tabCornerRadiusPill

                    SettingsSpinBox {
                        id: tabCornerRadiusSpin

                        accessibleName: i18n("Tab corner radius")
                        // Floors at 0, not at the sentinel: the pill is the
                        // switch's job, and letting this spin reach -1 would
                        // give the same state two controls.
                        from: 0
                        to: root._scrollConsts.tabCornerRadiusMax
                        stepSize: 1
                        onValueModified: value => appSettings.scrollingTabIndicatorCornerRadius = value
                        // Clamped at 0 on the way in so the pill sentinel shows
                        // as a square rather than as a nonsensical -1.
                        Binding on value {
                            value: Math.max(0, appSettings.scrollingTabIndicatorCornerRadius)
                            when: !tabCornerRadiusSpin.editing
                            restoreMode: Binding.RestoreNone
                        }
                    }
                }
            }
        }

        // =================================================================
        // Colors Card
        // =================================================================
        // Each colour is EMPTY by default, meaning "follow the color
        // scheme", so every row pairs its swatch with a reset that clears
        // back to empty rather than picking some hardcoded default.
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Colors")
            searchAnchor: "tabIndicatorColors"
            collapsible: true
            enabled: root.indicatorOn

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                ThemeFallbackColorRow {
                    Layout.fillWidth: true
                    title: i18n("Active tab")
                    searchAnchor: "tabIndicatorActiveColor"
                    description: i18n("The tab of the window the column is currently showing.")
                    storedColor: appSettings.scrollingTabIndicatorActiveColor
                    themeColor: Kirigami.Theme.highlightColor
                    picker: tabColorDialog
                    onColorChosen: function (hex) {
                        appSettings.scrollingTabIndicatorActiveColor = hex;
                    }
                }

                ThemeFallbackColorRow {
                    Layout.fillWidth: true
                    title: i18n("Inactive tabs")
                    searchAnchor: "tabIndicatorInactiveColor"
                    description: i18n("The tabs of the column's other windows.")
                    storedColor: appSettings.scrollingTabIndicatorInactiveColor
                    themeColor: Qt.alpha(Kirigami.Theme.textColor, 0.35)
                    picker: tabColorDialog
                    onColorChosen: function (hex) {
                        appSettings.scrollingTabIndicatorInactiveColor = hex;
                    }
                }

                ThemeFallbackColorRow {
                    Layout.fillWidth: true
                    title: i18n("Urgent tab")
                    searchAnchor: "tabIndicatorUrgentColor"
                    description: i18n("The tab of a window that is asking for attention.")
                    storedColor: appSettings.scrollingTabIndicatorUrgentColor
                    themeColor: Kirigami.Theme.negativeTextColor
                    picker: tabColorDialog
                    onColorChosen: function (hex) {
                        appSettings.scrollingTabIndicatorUrgentColor = hex;
                    }
                }
            }
        }
    }
}
