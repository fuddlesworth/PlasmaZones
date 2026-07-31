// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The scrolling Window Handling card, the peer of
 * TilingWindowHandlingCard and SnappingWindowHandlingCard. All rows bind the
 * appSettings context property, so the card carries no per-page state.
 *
 * Smart gaps is deliberately absent: scrolling reads the shared
 * Tiling.Gaps/SmartGaps value, so the tiling toggle governs both engines.
 */
SettingsCard {
    id: card

    headerText: i18n("Window Handling")
    searchAnchor: "scrollingWindowHandling"
    collapsible: true
    scopeEnabled: true
    scopeAppSettings: settingsController
    // The Window sub-domain of the per-screen scrolling map (InsertPosition +
    // RespectMinimumSize) — the other rows on this card are app-wide only.
    scopeHasOverridesMethod: "hasPerScreenScrollingWindowSettings"
    scopeClearerMethod: "clearPerScreenScrollingWindowSettings"

    // Adjust-step bounds, read once from ConfigDefaults via the controller.
    readonly property var _stepConsts: settingsController.scrollingWidthConstants()

    property PerScreenOverrideHelper psHelper: PerScreenOverrideHelper {
        appSettings: settingsController
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenScrollingSettings"
        setterMethod: "setPerScreenScrollingSetting"
    }

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
                storedValue: card.psHelper.settingValue("InsertPosition", appSettings.scrollingInsertPosition)
                onActivated: card.psHelper.writeSetting("InsertPosition", currentValue, function (v) {
                    appSettings.scrollingInsertPosition = v;
                })
            }
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Respect minimum size")
            searchAnchor: "scrollingRespectMinimumSize"
            description: i18n("Keep columns at least as wide and tall as their windows' minimum size, which can push other windows off screen")

            SettingsSwitch {
                checked: card.psHelper.settingValue("RespectMinimumSize", appSettings.scrollingRespectMinimumSize)
                accessibleName: i18n("Respect window minimum size")
                onToggled: function (newValue) {
                    card.psHelper.writeSetting("RespectMinimumSize", newValue, function (v) {
                        appSettings.scrollingRespectMinimumSize = v;
                    });
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
            description: i18n("When a floated window reopens, it returns to the position it was on instead of wherever the compositor would place it. A rule can override this either way, opting individual windows in or out.")

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
            description: i18n("How much of the screen width one press of the increase or decrease column width shortcut moves")

            SettingsSlider {
                accessibleName: i18n("Column width adjustment step")
                from: _stepConsts.stepPercentMin
                to: _stepConsts.stepPercentMax
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
            description: i18n("How much of the screen height one press of the increase or decrease window height shortcut moves")

            SettingsSlider {
                accessibleName: i18n("Window height adjustment step")
                from: _stepConsts.stepPercentMin
                to: _stepConsts.stepPercentMax
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
