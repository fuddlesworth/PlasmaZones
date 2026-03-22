// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int paddingMax: 50
    // Per-screen snapping gap/padding helper
    property alias selectedSnappingScreenName: snappingHelper.selectedScreenName
    readonly property alias isPerScreenSnapping: snappingHelper.isPerScreen
    readonly property alias hasSnappingOverrides: snappingHelper.hasOverrides

    function snappingSettingValue(key, globalValue) {
        return snappingHelper.settingValue(key, globalValue);
    }

    function writeSnappingSetting(key, value, globalSetter) {
        snappingHelper.writeSetting(key, value, globalSetter);
    }

    function clearSnappingOverrides() {
        snappingHelper.clearOverrides();
    }

    contentHeight: content.implicitHeight
    clip: true

    PerScreenOverrideHelper {
        id: snappingHelper

        appSettings: settingsController
        getterMethod: "getPerScreenSnappingSettings"
        setterMethod: "setPerScreenSnappingSetting"
        clearerMethod: "clearPerScreenSnappingSettings"
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =====================================================================
        // PER-MONITOR SNAPPING OVERRIDES
        // =====================================================================
        MonitorSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController
            featureEnabled: settingsController.settings.snappingEnabled
            selectedScreenName: snappingHelper.selectedScreenName
            hasOverrides: snappingHelper.hasOverrides
            onSelectedScreenNameChanged: snappingHelper.selectedScreenName = selectedScreenName
            onResetClicked: snappingHelper.clearOverrides()
        }

        // =====================================================================
        // ZONE GEOMETRY
        // =====================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: gapsCard.implicitHeight

            SettingsCard {
                id: gapsCard

                anchors.fill: parent
                headerText: i18n("Gaps")
                collapsible: true

                contentItem: Kirigami.FormLayout {
                    SettingsSpinBox {
                        formLabel: i18n("Zone padding:")
                        from: 0
                        to: root.paddingMax
                        value: root.snappingSettingValue("ZonePadding", appSettings.zonePadding)
                        onValueModified: (value) => {
                            return root.writeSnappingSetting("ZonePadding", value, function(v) {
                                appSettings.zonePadding = v;
                            });
                        }
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Edge gap:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: root.snappingSettingValue("OuterGap", appSettings.outerGap)
                            enabled: !perSideCheck.checked
                            onValueModified: root.writeSnappingSetting("OuterGap", value, function(v) {
                                appSettings.outerGap = v;
                            })
                            Accessible.name: i18n("Edge gap")
                        }

                        Label {
                            text: i18n("px")
                            visible: !perSideCheck.checked
                        }

                        CheckBox {
                            id: perSideCheck

                            text: i18n("Set per side")
                            checked: root.snappingSettingValue("UsePerSideOuterGap", appSettings.usePerSideOuterGap)
                            onToggled: root.writeSnappingSetting("UsePerSideOuterGap", checked, function(v) {
                                appSettings.usePerSideOuterGap = v;
                            })
                        }

                    }

                    GridLayout {
                        Kirigami.FormData.label: i18n("Per-side gaps:")
                        visible: perSideCheck.checked
                        columns: 6
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Top:")
                        }

                        SpinBox {
                            from: 0
                            to: root.paddingMax
                            value: root.snappingSettingValue("OuterGapTop", appSettings.outerGapTop)
                            onValueModified: root.writeSnappingSetting("OuterGapTop", value, function(v) {
                                appSettings.outerGapTop = v;
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
                            to: root.paddingMax
                            value: root.snappingSettingValue("OuterGapBottom", appSettings.outerGapBottom)
                            onValueModified: root.writeSnappingSetting("OuterGapBottom", value, function(v) {
                                appSettings.outerGapBottom = v;
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
                            to: root.paddingMax
                            value: root.snappingSettingValue("OuterGapLeft", appSettings.outerGapLeft)
                            onValueModified: root.writeSnappingSetting("OuterGapLeft", value, function(v) {
                                appSettings.outerGapLeft = v;
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
                            to: root.paddingMax
                            value: root.snappingSettingValue("OuterGapRight", appSettings.outerGapRight)
                            onValueModified: root.writeSnappingSetting("OuterGapRight", value, function(v) {
                                appSettings.outerGapRight = v;
                            })
                            Accessible.name: i18nc("@label", "Right edge gap")
                        }

                        Label {
                            text: i18nc("@label", "px")
                        }

                    }

                }

            }

        }

    }

}
