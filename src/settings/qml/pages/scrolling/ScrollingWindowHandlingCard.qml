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
 * Smart gaps is deliberately absent: scrolling reads the shared
 * Tiling.Gaps/SmartGaps value, so the tiling toggle governs both engines.
 */
SettingsCard {
    id: root

    headerText: i18n("Window handling")
    searchAnchor: "scrollingWindowHandling"
    collapsible: true

    // Adjust-step bounds, read once from ConfigDefaults via the controller.
    readonly property var _stepConsts: settingsController.scrollingConstants()

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
            description: i18n("How far the increase and decrease column width shortcuts move a column per press, as a share of the screen width")

            SettingsSlider {
                accessibleName: i18n("Column width adjustment step")
                from: root._stepConsts.stepPercentMin
                to: root._stepConsts.stepPercentMax
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
            description: i18n("How far the increase and decrease window height shortcuts resize a window per press, as a share of the screen height")

            SettingsSlider {
                accessibleName: i18n("Window height adjustment step")
                from: root._stepConsts.stepPercentMin
                to: root._stepConsts.stepPercentMax
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
    }
}
