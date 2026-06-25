// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
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

        // Colors / Decorations / Borders — shared with Tiling → Appearance via
        // AppearanceFacetCards. These are global (not per-screen), so they carry
        // no scope chrome.
        AppearanceFacetCards {
            Layout.fillWidth: true

            useSystemBorderColors: appSettings.snappingUseSystemBorderColors
            activeBorderColor: appSettings.snappingBorderColor
            inactiveBorderColor: appSettings.snappingInactiveBorderColor
            activeColorDescription: i18n("Border color for the focused snapped window")
            inactiveColorDescription: i18n("Border color for unfocused snapped windows")
            hideTitleBars: appSettings.snappingHideTitleBars
            hideTitleBarsDescription: i18n("Remove window title bars while snapped, restored when floating")
            hideTitleBarsAccessibleName: i18n("Hide title bars on snapped windows")
            showBorder: appSettings.snappingShowBorder
            borderWidth: appSettings.snappingBorderWidth
            borderWidthMin: root.settingsBridge.snappingBorderWidthMin
            borderWidthMax: root.settingsBridge.snappingBorderWidthMax
            borderWidthDescription: i18n("Thickness of colored borders around snapped windows")
            borderRadius: appSettings.snappingBorderRadius
            borderRadiusMin: root.settingsBridge.snappingBorderRadiusMin
            borderRadiusMax: root.settingsBridge.snappingBorderRadiusMax

            onUseSystemBorderColorsToggled: checked => {
                return appSettings.snappingUseSystemBorderColors = checked;
            }
            onActiveBorderColorPicked: selectedColor => {
                return appSettings.snappingBorderColor = selectedColor;
            }
            onInactiveBorderColorPicked: selectedColor => {
                return appSettings.snappingInactiveBorderColor = selectedColor;
            }
            onHideTitleBarsToggled: checked => {
                return appSettings.snappingHideTitleBars = checked;
            }
            onShowBorderToggled: checked => {
                return appSettings.snappingShowBorder = checked;
            }
            onBorderWidthModified: value => {
                return appSettings.snappingBorderWidth = value;
            }
            onBorderRadiusModified: value => {
                return appSettings.snappingBorderRadius = value;
            }
        }

        // =================================================================
        // Gaps (per-screen) — the card opts into the header scope chip; the
        // global cards above carry no scope chrome.
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            searchAnchor: "gaps"
            scopeEnabled: true
            scopeAppSettings: settingsController
            scopeHasOverridesMethod: "hasPerScreenSnappingSettings"
            scopeClearerMethod: "clearPerScreenSnappingSettings"
            provenanceEnabled: true
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
}
