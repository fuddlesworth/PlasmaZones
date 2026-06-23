// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.tilingAppearancePage
    readonly property int gapMax: root.settingsBridge.autotileGapMax
    // Per-screen override helper. Only the Gaps card below is per-screen; the
    // colour / decoration / border cards are global. The Gaps card opts into
    // the header scope chip, so the global cards above carry no scope chrome
    // and read as global.
    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: psHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // Colors Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Colors")
            searchAnchor: "colors"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Use system accent color")
                    searchAnchor: "useSystemAccentColor"
                    description: i18n("Derive border colors from your system color scheme")

                    SettingsSwitch {
                        id: useSystemColorsSwitch

                        checked: appSettings.autotileUseSystemBorderColors
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            appSettings.autotileUseSystemBorderColors = newValue;
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Active border color")
                    searchAnchor: "activeBorderColor"
                    description: i18n("Border color for the focused window")

                    ColorSwatchRow {
                        color: appSettings.autotileBorderColor
                        onClicked: {
                            activeBorderColorDialog.selectedColor = appSettings.autotileBorderColor;
                            activeBorderColorDialog.open();
                        }
                    }
                }

                SettingsSeparator {
                    visible: !useSystemColorsSwitch.checked
                }

                SettingsRow {
                    visible: !useSystemColorsSwitch.checked
                    title: i18n("Inactive border color")
                    searchAnchor: "inactiveBorderColor"
                    description: i18n("Border color for unfocused windows")

                    ColorSwatchRow {
                        color: appSettings.autotileInactiveBorderColor
                        onClicked: {
                            inactiveBorderColorDialog.selectedColor = appSettings.autotileInactiveBorderColor;
                            inactiveBorderColorDialog.open();
                        }
                    }
                }
            }
        }

        // =================================================================
        // Decorations Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Decorations")
            searchAnchor: "decorations"
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Hide title bars")
                    searchAnchor: "hideTitleBars"
                    description: i18n("Remove window title bars while autotiled, restored when floating")

                    SettingsSwitch {
                        checked: appSettings.autotileHideTitleBars
                        accessibleName: i18n("Hide title bars on tiled windows")
                        onToggled: function (newValue) {
                            appSettings.autotileHideTitleBars = newValue;
                        }
                    }
                }
            }
        }

        // =================================================================
        // Borders Card
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Borders")
            searchAnchor: "borders"
            showToggle: true
            toggleChecked: appSettings.autotileShowBorder
            onToggleClicked: checked => {
                return appSettings.autotileShowBorder = checked;
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Border width")
                    searchAnchor: "borderWidth"
                    description: i18n("Thickness of colored borders around tiled windows")

                    SettingsSpinBox {
                        from: root.settingsBridge.autotileBorderWidthMin
                        to: root.settingsBridge.autotileBorderWidthMax
                        value: appSettings.autotileBorderWidth
                        onValueModified: value => {
                            return appSettings.autotileBorderWidth = value;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Corner radius")
                    searchAnchor: "cornerRadius"
                    description: i18n("Roundness of border corners (0 for square)")

                    SettingsSpinBox {
                        from: root.settingsBridge.autotileBorderRadiusMin
                        to: root.settingsBridge.autotileBorderRadiusMax
                        value: appSettings.autotileBorderRadius
                        onValueModified: value => {
                            return appSettings.autotileBorderRadius = value;
                        }
                    }
                }
            }
        }

        // =================================================================
        // Gaps (per-screen) — the card opts into the header scope chip, so it
        // reads as a normal global card until you pick a monitor. The global
        // cards above carry no scope chrome, keeping their scope unambiguous.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            scopeEnabled: true
            scopeAppSettings: settingsController
            // Gaps sub-domain only — must not report/reset the Algorithm card's
            // per-monitor overrides (shared autotile map, disjoint key subsets).
            scopeHasOverridesMethod: "hasPerScreenAutotileGapsSettings"
            scopeClearerMethod: "clearPerScreenAutotileGapsSettings"
            gapMax: root.gapMax
            gapMin: root.settingsBridge.autotileGapMin
            primaryGapMin: root.settingsBridge.autotileInnerGapMin
            primaryGapMax: root.settingsBridge.autotileInnerGapMax
            primaryGapValue: root.settingValue("InnerGap", appSettings.autotileInnerGap)
            outerGapValue: root.settingValue("OuterGap", appSettings.autotileOuterGap)
            usePerSideOuterGap: root.settingValue("UsePerSideOuterGap", appSettings.autotileUsePerSideOuterGap)
            smartGapsValue: root.settingValue("SmartGaps", appSettings.autotileSmartGaps)
            outerGapTopValue: root.settingValue("OuterGapTop", appSettings.autotileOuterGapTop)
            outerGapBottomValue: root.settingValue("OuterGapBottom", appSettings.autotileOuterGapBottom)
            outerGapLeftValue: root.settingValue("OuterGapLeft", appSettings.autotileOuterGapLeft)
            outerGapRightValue: root.settingValue("OuterGapRight", appSettings.autotileOuterGapRight)
            onPrimaryGapModified: value => {
                return root.writeSetting("InnerGap", value, function (v) {
                    appSettings.autotileInnerGap = v;
                });
            }
            onOuterGapModified: value => {
                return root.writeSetting("OuterGap", value, function (v) {
                    appSettings.autotileOuterGap = v;
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeSetting("UsePerSideOuterGap", checked, function (v) {
                    appSettings.autotileUsePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: value => {
                return root.writeSetting("OuterGapTop", value, function (v) {
                    appSettings.autotileOuterGapTop = v;
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeSetting("OuterGapBottom", value, function (v) {
                    appSettings.autotileOuterGapBottom = v;
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeSetting("OuterGapLeft", value, function (v) {
                    appSettings.autotileOuterGapLeft = v;
                });
            }
            onOuterGapRightModified: value => {
                return root.writeSetting("OuterGapRight", value, function (v) {
                    appSettings.autotileOuterGapRight = v;
                });
            }
            onSmartGapsToggled: checked => {
                return root.writeSetting("SmartGaps", checked, function (v) {
                    appSettings.autotileSmartGaps = v;
                });
            }
        }
    }

    // =====================================================================
    // Color Dialogs
    // =====================================================================
    ColorDialog {
        id: activeBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Active Border Color")
        onAccepted: appSettings.autotileBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: appSettings.autotileInactiveBorderColor = selectedColor
    }
}
