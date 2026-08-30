// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// QtQuick is load-bearing here: the Accessible attached type on the combo
// boxes below comes from it, not from Layouts or Kirigami.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The scrolling Window Handling card, the peer of
 * TilingWindowHandlingCard and SnappingWindowHandlingCard. All rows bind the
 * appSettings context property, so the card carries no per-page state —
 * app-wide only like its two siblings; per-context insert position is a
 * rules job (the SetScrollInsertPosition context action).
 *
 * Smart gaps lives here rather than being borrowed from tiling: the gap
 * VALUES are shared and mode-neutral, but whether a single column drops them
 * is per-mode behaviour, and scrolling keeps its own under Scrolling.
 */
SettingsCard {
    id: root

    headerText: i18n("Window handling")
    searchAnchor: "scrollingWindowHandling"
    collapsible: true

    // Adjust-step bounds, read once from ConfigDefaults via the controller.
    readonly property var _scrollConsts: settingsController.scrollingConstants()

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        SettingsRow {
            title: i18n("New window placement")
            searchAnchor: "scrollingNewWindowPlacement"
            description: i18n("Where a new window's column enters the strip. Restored windows and per-window rules keep their own position.")

            WideComboBox {
                Accessible.name: i18n("New window placement")
                textRole: "text"
                valueRole: "value"
                model: settingsController.valueOptions("Scrolling.Behavior", "InsertPosition")
                storedValue: appSettings.scrollingInsertPosition
                onActivated: appSettings.scrollingInsertPosition = currentValue
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Smart gaps")
            searchAnchor: "scrollingSmartGaps"
            description: i18n("Remove the outer gaps while the strip holds a single column, so that column sits against the screen edge at its own width")

            SettingsSwitch {
                checked: appSettings.scrollingSmartGaps
                accessibleName: i18n("Smart gaps")
                onToggled: function (newValue) {
                    appSettings.scrollingSmartGaps = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Respect minimum size")
            searchAnchor: "scrollingRespectMinimumSize"
            description: i18n("Keep columns at least as wide and tall as their windows' minimum size, which can push other windows off screen")

            SettingsSwitch {
                checked: appSettings.scrollingRespectMinimumSize
                accessibleName: i18n("Respect window minimum size")
                onToggled: function (newValue) {
                    appSettings.scrollingRespectMinimumSize = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Center short columns")
            searchAnchor: "scrollingCenterShortColumns"
            description: i18n("Center the windows of a column that does not fill the screen, rather than leaving the unused space below them")

            SettingsSwitch {
                checked: appSettings.scrollingCenterShortColumns
                accessibleName: i18n("Center short columns")
                onToggled: function (newValue) {
                    appSettings.scrollingCenterShortColumns = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Restore columns on login")
            searchAnchor: "scrollingRestoreStripsOnLogin"
            description: i18n("When windows reopen after a restart, rebuild their columns with the same order, widths, and tab groups")

            SettingsSwitch {
                checked: appSettings.scrollingRestoreStripsOnLogin
                accessibleName: i18n("Restore columns on login")
                onToggled: function (newValue) {
                    appSettings.scrollingRestoreStripsOnLogin = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Restore floated windows to their previous position")
            searchAnchor: "scrollingRestoreFloatedOnLogin"
            description: i18n("When a floated window reopens, it returns to the position and size it had before, rather than being placed by the compositor. A rule can override this either way, opting individual windows in or out.")

            SettingsSwitch {
                checked: appSettings.scrollingRestoreFloatedWindowsOnLogin
                accessibleName: i18n("Restore floated windows to their previous position")
                onToggled: function (newValue) {
                    appSettings.scrollingRestoreFloatedWindowsOnLogin = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Keep floating windows above")
            searchAnchor: "scrollingKeepFloatingAbove"
            description: i18n("Keep the windows you float stacked above the columns of the strip. A rule that sets a window layer takes precedence for the windows it matches.")

            SettingsSwitch {
                checked: appSettings.scrollingKeepFloatingAbove
                accessibleName: i18n("Keep floating windows above")
                onToggled: function (newValue) {
                    appSettings.scrollingKeepFloatingAbove = newValue;
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Sticky windows")
            searchAnchor: "scrollingStickyWindows"
            description: i18n("How to handle windows that are shown on all virtual desktops")

            WideComboBox {
                Accessible.name: i18n("Sticky window handling")
                textRole: "text"
                valueRole: "value"
                model: settingsController.valueOptions("Scrolling.Behavior", "StickyWindowHandling")
                storedValue: appSettings.scrollingStickyWindowHandling
                onActivated: appSettings.scrollingStickyWindowHandling = currentValue
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Width adjustment step")
            searchAnchor: "scrollingColumnWidthStep"
            description: i18n("How far the increase and decrease column width shortcuts resize a column per press, as a share of the strip")

            SettingsSlider {
                accessibleName: i18n("Column width adjustment step")
                from: root._scrollConsts.stepPercentMin
                to: root._scrollConsts.stepPercentMax
                stepSize: 1
                value: appSettings.scrollingColumnWidthStepPercent
                formatValue: function (v) {
                    return Math.round(v) + "%";
                }
                onMoved: function (newValue) {
                    appSettings.scrollingColumnWidthStepPercent = Math.round(newValue);
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Height adjustment step")
            searchAnchor: "scrollingWindowHeightStep"
            description: i18n("How far the increase and decrease window height shortcuts resize a window per press, as a share of the work area across the strip")

            SettingsSlider {
                accessibleName: i18n("Window height adjustment step")
                from: root._scrollConsts.stepPercentMin
                to: root._scrollConsts.stepPercentMax
                stepSize: 1
                value: appSettings.scrollingWindowHeightStepPercent
                formatValue: function (v) {
                    return Math.round(v) + "%";
                }
                onMoved: function (newValue) {
                    appSettings.scrollingWindowHeightStepPercent = Math.round(newValue);
                }
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("View scroll step")
            searchAnchor: "scrollingViewScrollStep"
            description: i18n("How far one notch of Meta+Shift+wheel moves the strip without changing focus, as a share of the work area along the strip")

            SettingsSlider {
                accessibleName: i18n("View scroll step")
                from: root._scrollConsts.stepPercentMin
                to: root._scrollConsts.stepPercentMax
                stepSize: 1
                value: appSettings.scrollingViewScrollStepPercent
                formatValue: function (v) {
                    return Math.round(v) + "%";
                }
                onMoved: function (newValue) {
                    appSettings.scrollingViewScrollStepPercent = Math.round(newValue);
                }
            }
        }
    }
}
