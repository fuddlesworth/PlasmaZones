// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsFlickable {
    id: root

    readonly property var settingsBridge: settingsController.snappingWindowAppearancePage
    // Per-screen snapping gap/padding helper. Only the Gaps card below is
    // per-screen; the colour / decoration / border cards are global. The Gaps
    // card opts into the header scope chip, so the global cards above it carry
    // no scope chrome and read as global.

    function snappingSettingValue(key, globalValue) {
        return snappingHelper.settingValue(key, globalValue);
    }

    function writeSnappingSetting(key, value, globalSetter) {
        snappingHelper.writeSetting(key, value, globalSetter);
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: snappingHelper

        appSettings: settingsController
        // Shared app-wide scope — a monitor picked on any per-monitor page
        // stays picked here.
        selectedScreenName: settingsController.scopeScreenName
        getterMethod: "getPerScreenSnappingSettings"
        setterMethod: "setPerScreenSnappingSetting"
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

                        checked: appSettings.snappingUseSystemBorderColors
                        accessibleName: i18n("Use system accent color")
                        onToggled: function (newValue) {
                            appSettings.snappingUseSystemBorderColors = newValue;
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
                    description: i18n("Border color for the focused snapped window")

                    ColorSwatchRow {
                        color: appSettings.snappingBorderColor
                        onClicked: {
                            activeBorderColorDialog.selectedColor = appSettings.snappingBorderColor;
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
                    description: i18n("Border color for unfocused snapped windows")

                    ColorSwatchRow {
                        color: appSettings.snappingInactiveBorderColor
                        onClicked: {
                            inactiveBorderColorDialog.selectedColor = appSettings.snappingInactiveBorderColor;
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
                    description: i18n("Remove window title bars while snapped, restored when floating")

                    SettingsSwitch {
                        checked: appSettings.snappingHideTitleBars
                        accessibleName: i18n("Hide title bars on snapped windows")
                        onToggled: function (newValue) {
                            appSettings.snappingHideTitleBars = newValue;
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
            toggleChecked: appSettings.snappingShowBorder
            onToggleClicked: checked => {
                return appSettings.snappingShowBorder = checked;
            }
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                SettingsRow {
                    title: i18n("Border width")
                    searchAnchor: "borderWidth"
                    description: i18n("Thickness of colored borders around snapped windows")

                    SettingsSpinBox {
                        from: root.settingsBridge.snappingBorderWidthMin
                        to: root.settingsBridge.snappingBorderWidthMax
                        value: appSettings.snappingBorderWidth
                        onValueModified: value => {
                            return appSettings.snappingBorderWidth = value;
                        }
                    }
                }

                SettingsSeparator {}

                SettingsRow {
                    title: i18n("Corner radius")
                    searchAnchor: "cornerRadius"
                    description: i18n("Roundness of border corners (0 for square)")

                    SettingsSpinBox {
                        from: root.settingsBridge.snappingBorderRadiusMin
                        to: root.settingsBridge.snappingBorderRadiusMax
                        value: appSettings.snappingBorderRadius
                        onValueModified: value => {
                            return appSettings.snappingBorderRadius = value;
                        }
                    }
                }
            }
        }

        // =================================================================
        // Gaps (per-screen) — the card opts into the header scope chip; the
        // global cards above carry no scope chrome.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenSnappingGapsSettings"
            scopeClearerMethod: "clearPerScreenSnappingGapsSettings"
            // Snapping shares the "Inner gap" / "Outer gap" labels with tiling
            // (consistent cross-mode wording) but has no Smart gaps. Inner-gap
            // bounds come from zonePaddingMin/Max; the outer / per-side gaps from
            // gapMin/Max — each matching its validator clamp.
            primaryGapLabel: i18n("Inner gap")
            primaryGapDescription: i18n("Space between snapped windows")
            outerGapLabel: i18n("Outer gap")
            outerGapDescription: i18n("Space from screen edges to snapped windows")
            showSmartGaps: false
            gapMin: root.settingsBridge.gapMin
            gapMax: root.settingsBridge.gapMax
            primaryGapMin: root.settingsBridge.zonePaddingMin
            primaryGapMax: root.settingsBridge.zonePaddingMax
            primaryGapValue: root.snappingSettingValue("ZonePadding", appSettings.zonePadding)
            outerGapValue: root.snappingSettingValue("OuterGap", appSettings.outerGap)
            usePerSideOuterGap: root.snappingSettingValue("UsePerSideOuterGap", appSettings.usePerSideOuterGap)
            outerGapTopValue: root.snappingSettingValue("OuterGapTop", appSettings.outerGapTop)
            outerGapBottomValue: root.snappingSettingValue("OuterGapBottom", appSettings.outerGapBottom)
            outerGapLeftValue: root.snappingSettingValue("OuterGapLeft", appSettings.outerGapLeft)
            outerGapRightValue: root.snappingSettingValue("OuterGapRight", appSettings.outerGapRight)
            onPrimaryGapModified: value => {
                return root.writeSnappingSetting("ZonePadding", value, function (v) {
                    appSettings.zonePadding = v;
                });
            }
            onOuterGapModified: value => {
                return root.writeSnappingSetting("OuterGap", value, function (v) {
                    appSettings.outerGap = v;
                });
            }
            onUsePerSideOuterGapToggled: checked => {
                return root.writeSnappingSetting("UsePerSideOuterGap", checked, function (v) {
                    appSettings.usePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: value => {
                return root.writeSnappingSetting("OuterGapTop", value, function (v) {
                    appSettings.outerGapTop = v;
                });
            }
            onOuterGapBottomModified: value => {
                return root.writeSnappingSetting("OuterGapBottom", value, function (v) {
                    appSettings.outerGapBottom = v;
                });
            }
            onOuterGapLeftModified: value => {
                return root.writeSnappingSetting("OuterGapLeft", value, function (v) {
                    appSettings.outerGapLeft = v;
                });
            }
            onOuterGapRightModified: value => {
                return root.writeSnappingSetting("OuterGapRight", value, function (v) {
                    appSettings.outerGapRight = v;
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
        onAccepted: appSettings.snappingBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        options: ColorDialog.ShowAlphaChannel
        title: i18n("Choose Inactive Border Color")
        onAccepted: appSettings.snappingInactiveBorderColor = selectedColor
    }
}
