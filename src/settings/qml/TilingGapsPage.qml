// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int gapMax: 50
    // Per-screen override helper
    property alias selectedScreenName: psHelper.selectedScreenName
    readonly property alias isPerScreen: psHelper.isPerScreen
    readonly property alias hasOverrides: psHelper.hasOverrides

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
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
        clearerMethod: "clearPerScreenAutotileSettings"
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
            featureEnabled: settingsController.settings.autotileEnabled
            selectedScreenName: root.selectedScreenName
            hasOverrides: root.hasOverrides
            onSelectedScreenNameChanged: root.selectedScreenName = selectedScreenName
            onResetClicked: psHelper.clearOverrides()
        }

        // =================================================================
        // Gaps Card (per-monitor)
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Gaps")
            collapsible: true

            contentItem: Kirigami.FormLayout {
                SettingsSpinBox {
                    formLabel: i18n("Inner gap:")
                    from: 0
                    to: root.gapMax
                    value: root.settingValue("InnerGap", appSettings.autotileInnerGap)
                    tooltipText: i18n("Gap between tiled windows")
                    onValueModified: (value) => {
                        return root.writeSetting("InnerGap", value, function(v) {
                            appSettings.autotileInnerGap = v;
                        });
                    }
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Outer gap:")
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: root.settingValue("OuterGap", appSettings.autotileOuterGap)
                        enabled: !tilePerSideCheck.checked
                        onValueModified: root.writeSetting("OuterGap", value, function(v) {
                            appSettings.autotileOuterGap = v;
                        })
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Gap from screen edges")
                    }

                    Label {
                        text: i18n("px")
                        visible: !tilePerSideCheck.checked
                    }

                    CheckBox {
                        id: tilePerSideCheck

                        text: i18n("Set per side")
                        checked: root.settingValue("UsePerSideOuterGap", appSettings.autotileUsePerSideOuterGap)
                        onToggled: root.writeSetting("UsePerSideOuterGap", checked, function(v) {
                            appSettings.autotileUsePerSideOuterGap = v;
                        })
                    }

                }

                GridLayout {
                    Kirigami.FormData.label: i18n("Per-side gaps:")
                    visible: tilePerSideCheck.checked
                    columns: 6
                    columnSpacing: Kirigami.Units.smallSpacing
                    rowSpacing: Kirigami.Units.smallSpacing

                    Label {
                        text: i18n("Top:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: root.settingValue("OuterGapTop", appSettings.autotileOuterGapTop)
                        onValueModified: root.writeSetting("OuterGapTop", value, function(v) {
                            appSettings.autotileOuterGapTop = v;
                        })
                        Accessible.name: i18nc("@label", "Top edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                    Label {
                        text: i18n("Bottom:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: root.settingValue("OuterGapBottom", appSettings.autotileOuterGapBottom)
                        onValueModified: root.writeSetting("OuterGapBottom", value, function(v) {
                            appSettings.autotileOuterGapBottom = v;
                        })
                        Accessible.name: i18nc("@label", "Bottom edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                    Label {
                        text: i18n("Left:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: root.settingValue("OuterGapLeft", appSettings.autotileOuterGapLeft)
                        onValueModified: root.writeSetting("OuterGapLeft", value, function(v) {
                            appSettings.autotileOuterGapLeft = v;
                        })
                        Accessible.name: i18nc("@label", "Left edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                    Label {
                        text: i18n("Right:")
                    }

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: root.settingValue("OuterGapRight", appSettings.autotileOuterGapRight)
                        onValueModified: root.writeSetting("OuterGapRight", value, function(v) {
                            appSettings.autotileOuterGapRight = v;
                        })
                        Accessible.name: i18nc("@label", "Right edge gap")
                    }

                    Label {
                        text: i18nc("@label", "px")
                    }

                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Smart gaps:")
                    text: i18n("Hide gaps when only one window is tiled")
                    checked: root.settingValue("SmartGaps", appSettings.autotileSmartGaps)
                    onToggled: root.writeSetting("SmartGaps", checked, function(v) {
                        appSettings.autotileSmartGaps = v;
                    })
                }

            }

        }

    }

}
