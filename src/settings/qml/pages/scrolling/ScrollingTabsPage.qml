// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

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

    // The ISettings object (the `appSettings` context property), captured at
    // page scope. FontPickerDialog declares its own `appSettings:
    // settingsController` (the controller carries the QFontDatabase helper
    // invokables it needs), which SHADOWS the context property inside the
    // dialog's onAccepted, so a write to `appSettings.scrollingTabIndicatorFont*`
    // there would hit a nonexistent property on the controller and throw. The
    // font writes go through this reference instead (same capture
    // SnappingOverlayAppearancePage makes for the zone label font).
    readonly property var appSettingsObj: appSettings

    // PAGE-LEVEL, shared by the three colour rows: a page rebuild while the
    // dialog is open would destroy a row-scoped dialog and tear the popup down
    // under the user. (A card COLLAPSE would not — SettingsCard only drives
    // its clip height and never destroys the subtree.) Each row connects to `accepted`
    // transiently and disconnects on close, so sharing cannot cross wires.
    ColorDialog {
        id: tabColorDialog

        title: i18n("Choose Tab Color")
        options: ColorDialog.ShowAlphaChannel
    }

    // Publish the open state so page-stepping cannot swap the page out from
    // under the open dialog — same pattern (and standalone-host guard) as
    // RulesPage.
    readonly property bool anyModalOpen: tabColorDialog.visible || fontPickerDialog.visible
    onAnyModalOpenChanged: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = anyModalOpen;
    }
    // Clear a latched true on page swap (RulesPage's own teardown pattern).
    Component.onDestruction: {
        if (typeof window !== "undefined" && window && window._pageOwnedModalOpen !== undefined)
            window._pageOwnedModalOpen = false;
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

                SettingsSeparator {
                    enabled: root.indicatorOn
                }

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

                // Shape rather than metric, so it sits with Style and Position
                // instead of on the Size and spacing card. There is no size row
                // on purpose: the effect fits each title to the indicator, so
                // Thickness on the next card already is the size control.
                SettingsRow {
                    title: i18n("Font")
                    searchAnchor: "tabIndicatorFont"
                    description: i18n("Typeface and style for the titles on tab chips. A segment bar draws no titles, so it ignores this.")
                    enabled: root.indicatorOn

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Button {
                            text: appSettings.scrollingTabIndicatorFontFamily || i18n("System default")
                            font.family: appSettings.scrollingTabIndicatorFontFamily
                            font.weight: appSettings.scrollingTabIndicatorFontWeight
                            font.italic: appSettings.scrollingTabIndicatorFontItalic
                            icon.name: "font-select-symbolic"
                            Accessible.name: i18n("Tab title font")
                            onClicked: {
                                fontPickerDialog.selectedFamily = appSettings.scrollingTabIndicatorFontFamily;
                                fontPickerDialog.selectedWeight = appSettings.scrollingTabIndicatorFontWeight;
                                fontPickerDialog.selectedItalic = appSettings.scrollingTabIndicatorFontItalic;
                                fontPickerDialog.selectedUnderline = appSettings.scrollingTabIndicatorFontUnderline;
                                fontPickerDialog.selectedStrikeout = appSettings.scrollingTabIndicatorFontStrikeout;
                                fontPickerDialog.open();
                            }
                        }

                        Button {
                            icon.name: "edit-clear"
                            // The defaults: no family (follow the system font),
                            // bold, and none of the three effects.
                            visible: appSettings.scrollingTabIndicatorFontFamily !== "" || appSettings.scrollingTabIndicatorFontWeight !== Font.Bold || appSettings.scrollingTabIndicatorFontItalic || appSettings.scrollingTabIndicatorFontUnderline || appSettings.scrollingTabIndicatorFontStrikeout
                            Accessible.name: i18n("Reset the tab title font")
                            onClicked: {
                                appSettings.scrollingTabIndicatorFontFamily = "";
                                appSettings.scrollingTabIndicatorFontWeight = Font.Bold;
                                appSettings.scrollingTabIndicatorFontItalic = false;
                                appSettings.scrollingTabIndicatorFontUnderline = false;
                                appSettings.scrollingTabIndicatorFontStrikeout = false;
                            }
                        }
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
                    // Collapses out while the pill sentinel is stored. This
                    // says `enabled`, and SettingsRow is visible:enabled, so
                    // the row HIDES rather than greying — the component's
                    // documented policy for a setting that cannot apply, and
                    // the same thing every other conditional row on this page
                    // does. (An earlier note here claimed the opposite,
                    // "disabled, not hidden". It never was.) The consequence
                    // is that a deep link to this row's search anchor lands on
                    // a row that is not on screen until the sentinel is
                    // cleared, which it shares with every disabled row in the
                    // app rather than being specific to this one.
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

                // Swatch previews bind the SHARED fallback constants (see
                // ZoneColorDefaults) so they show exactly what the renderer
                // draws, including the style-dependent inactive fallback:
                // titled chips rest unfilled, so previewing the bar's grey
                // there would show a colour the indicator never paints.
                ThemeFallbackColorRow {
                    title: i18n("Active tab")
                    searchAnchor: "tabIndicatorActiveColor"
                    description: i18n("The tab of the window the column is currently showing.")
                    storedColor: appSettings.scrollingTabIndicatorActiveColor
                    themeColor: QFZCommon.ZoneColorDefaults.tabActiveColor
                    picker: tabColorDialog
                    onColorChosen: function (hex) {
                        appSettings.scrollingTabIndicatorActiveColor = hex;
                    }
                }

                SettingsSeparator {}

                ThemeFallbackColorRow {
                    title: i18n("Inactive tabs")
                    searchAnchor: "tabIndicatorInactiveColor"
                    description: i18n("The tabs of the column's other windows.")
                    storedColor: appSettings.scrollingTabIndicatorInactiveColor
                    // 0 = titled chips (see ISettings::scrollingTabIndicatorStyle).
                    themeColor: appSettings.scrollingTabIndicatorStyle === 0 ? QFZCommon.ZoneColorDefaults.tabInactiveChipColor : QFZCommon.ZoneColorDefaults.tabInactiveBarColor
                    picker: tabColorDialog
                    onColorChosen: function (hex) {
                        appSettings.scrollingTabIndicatorInactiveColor = hex;
                    }
                }

                SettingsSeparator {}

                ThemeFallbackColorRow {
                    title: i18n("Urgent tab")
                    searchAnchor: "tabIndicatorUrgentColor"
                    description: i18n("The tab of a window that is asking for attention.")
                    storedColor: appSettings.scrollingTabIndicatorUrgentColor
                    themeColor: QFZCommon.ZoneColorDefaults.tabUrgentColor
                    picker: tabColorDialog
                    onColorChosen: function (hex) {
                        appSettings.scrollingTabIndicatorUrgentColor = hex;
                    }
                }
            }
        }
    }

    // PAGE-LEVEL like the colour dialog above, for the same reason.
    FontPickerDialog {
        id: fontPickerDialog

        // The controller on purpose: it carries the QFontDatabase helper
        // invokables (fontStylesForFamily / fontStyleWeight / fontStyleItalic)
        // the dialog calls. It does NOT carry the scrollingTabIndicatorFont*
        // settings, so the writes below go through root.appSettingsObj rather
        // than the bare `appSettings` this declaration shadows in the dialog's
        // scope (see appSettingsObj above).
        appSettings: settingsController
        // The shared dialog names the zone labels in its own title, so say what
        // this instance edits instead.
        title: i18n("Choose Tab Font")
        onAccepted: {
            root.appSettingsObj.scrollingTabIndicatorFontFamily = selectedFamily;
            root.appSettingsObj.scrollingTabIndicatorFontWeight = selectedWeight;
            root.appSettingsObj.scrollingTabIndicatorFontItalic = selectedItalic;
            root.appSettingsObj.scrollingTabIndicatorFontUnderline = selectedUnderline;
            root.appSettingsObj.scrollingTabIndicatorFontStrikeout = selectedStrikeout;
        }
    }
}
