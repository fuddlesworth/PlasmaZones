// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Scrolling → Layout sub-page — niri-style scrollable tiling.
 *
 * Edits the scrolling-mode tuning — strip gaps, the width of a freshly opened
 * column, the viewport-centering mode, and the column-width / window-height
 * preset lists — as global defaults or per-monitor overrides, selected by the
 * MonitorSelectorSection (identical pattern to TilingAlgorithmPage). Every
 * value is routed through PerScreenOverrideHelper so a control writes either
 * the global Settings Q_PROPERTY or the screen's override map.
 */
SettingsFlickable {
    id: root

    // Per-screen override helper surface — mirrors TilingAlgorithmPage.
    property alias selectedScreenName: psHelper.selectedScreenName
    readonly property alias isPerScreen: psHelper.isPerScreen
    readonly property alias hasOverrides: psHelper.hasOverrides

    // Settings persists column / height presets and the default column width
    // as fractions [0..1]; the UI edits whole percents.
    function fractionToPercent(fraction) {
        return Math.round(fraction * 100);
    }

    // Resolve a value: per-screen override when a monitor is selected and it
    // carries the key, otherwise the global default.
    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    // Write a value: to the selected monitor's override map, or — when "All
    // Monitors" is selected — to the global setter.
    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        getterMethod: "getPerScreenScrollSettings"
        setterMethod: "setPerScreenScrollSetting"
        clearerMethod: "clearPerScreenScrollSettings"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Monitor Selector (per-screen overrides)
        // =================================================================
        MonitorSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController
            selectedScreenName: root.selectedScreenName
            hasOverrides: root.hasOverrides
            onSelectedScreenNameChanged: root.selectedScreenName = selectedScreenName
            onResetClicked: psHelper.clearOverrides()
        }

        // =================================================================
        // Gaps
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Gaps")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Inner gap")
                    description: i18n("Spacing between columns, and between windows stacked within a column")

                    SettingsSpinBox {
                        from: 0
                        // Matches the Scrolling.Gaps schema clamp
                        // (ConfigDefaults::scrollInnerGapMax) — a larger value
                        // would be silently truncated on save.
                        to: 50
                        value: root.settingValue("InnerGap", appSettings.scrollInnerGap)
                        unitText: i18n("px")
                        onValueModified: function(newValue) {
                            root.writeSetting("InnerGap", newValue, function(v) {
                                appSettings.scrollInnerGap = v;
                            });
                        }
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Outer gap")
                    description: i18n("Spacing between the strip and the screen edges")

                    SettingsSpinBox {
                        from: 0
                        // Matches the Scrolling.Gaps schema clamp
                        // (ConfigDefaults::scrollOuterGapMax) — a larger value
                        // would be silently truncated on save.
                        to: 50
                        value: root.settingValue("OuterGap", appSettings.scrollOuterGap)
                        unitText: i18n("px")
                        onValueModified: function(newValue) {
                            root.writeSetting("OuterGap", newValue, function(v) {
                                appSettings.scrollOuterGap = v;
                            });
                        }
                    }

                }

            }

        }

        // =================================================================
        // Layout
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Layout")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("New column width")
                    description: i18n("Width a freshly opened column takes, as a fraction of the screen")

                    SettingsSlider {
                        from: 10
                        to: 100
                        stepSize: 1
                        value: root.fractionToPercent(root.settingValue("DefaultColumnWidth", appSettings.scrollDefaultColumnWidth))
                        onMoved: function(newValue) {
                            root.writeSetting("DefaultColumnWidth", newValue / 100, function(v) {
                                appSettings.scrollDefaultColumnWidth = v;
                            });
                        }
                    }

                }

                SettingsSeparator {
                }

                SettingsRow {
                    title: i18n("Center focused column")
                    description: i18n("Keep the focused column centered in the viewport, instead of scrolling only as far as needed to reveal it")

                    SettingsSwitch {
                        checked: root.settingValue("CenterFocusedColumn", appSettings.scrollCenterFocusedColumn)
                        accessibleName: i18n("Center the focused column")
                        onToggled: function(newValue) {
                            root.writeSetting("CenterFocusedColumn", newValue, function(v) {
                                appSettings.scrollCenterFocusedColumn = v;
                            });
                        }
                    }

                }

            }

        }

        // =================================================================
        // Preset lists
        // =================================================================
        ScrollingPresetListCard {
            Layout.fillWidth: true
            headerText: i18n("Column width presets")
            description: i18n("Widths the cycle-column-width shortcut steps through, as a percentage of the screen")
            values: root.settingValue("PresetColumnWidths", appSettings.scrollPresetColumnWidths)
            onValuesModified: function(newValues) {
                root.writeSetting("PresetColumnWidths", newValues, function(v) {
                    appSettings.scrollPresetColumnWidths = v;
                });
            }
        }

        ScrollingPresetListCard {
            Layout.fillWidth: true
            headerText: i18n("Window height presets")
            description: i18n("Heights the cycle-window-height shortcut steps through, as a percentage of the column")
            values: root.settingValue("PresetWindowHeights", appSettings.scrollPresetWindowHeights)
            onValuesModified: function(newValues) {
                root.writeSetting("PresetWindowHeights", newValues, function(v) {
                    appSettings.scrollPresetWindowHeights = v;
                });
            }
        }

    }

}
