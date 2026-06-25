// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
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

        // Colors / Decorations / Borders — shared with Snapping → Window
        // Appearance via AppearanceFacetCards. These are global (not per-screen),
        // so they carry no scope chrome.
        AppearanceFacetCards {
            Layout.fillWidth: true

            useSystemBorderColors: appSettings.autotileUseSystemBorderColors
            activeBorderColor: appSettings.autotileBorderColor
            inactiveBorderColor: appSettings.autotileInactiveBorderColor
            hideTitleBars: appSettings.autotileHideTitleBars
            hideTitleBarsDescription: i18n("Remove window title bars while autotiled, restored when floating")
            showBorder: appSettings.autotileShowBorder
            borderWidth: appSettings.autotileBorderWidth
            borderWidthMin: root.settingsBridge.autotileBorderWidthMin
            borderWidthMax: root.settingsBridge.autotileBorderWidthMax
            borderRadius: appSettings.autotileBorderRadius
            borderRadiusMin: root.settingsBridge.autotileBorderRadiusMin
            borderRadiusMax: root.settingsBridge.autotileBorderRadiusMax

            onUseSystemBorderColorsToggled: checked => {
                return appSettings.autotileUseSystemBorderColors = checked;
            }
            onActiveBorderColorPicked: selectedColor => {
                return appSettings.autotileBorderColor = selectedColor;
            }
            onInactiveBorderColorPicked: selectedColor => {
                return appSettings.autotileInactiveBorderColor = selectedColor;
            }
            onHideTitleBarsToggled: checked => {
                return appSettings.autotileHideTitleBars = checked;
            }
            onShowBorderToggled: checked => {
                return appSettings.autotileShowBorder = checked;
            }
            onBorderWidthModified: value => {
                return appSettings.autotileBorderWidth = value;
            }
            onBorderRadiusModified: value => {
                return appSettings.autotileBorderRadius = value;
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
}
