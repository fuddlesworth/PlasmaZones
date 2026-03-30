// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int paddingMax: settingsController.gapMax
    // Zone selector constants (consumed by ZoneSelectorSection via constants property)
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 3
    readonly property int zoneSelectorTriggerMax: settingsController.zoneSelectorTriggerDistanceMax
    readonly property int zoneSelectorPreviewWidthMin: settingsController.zoneSelectorPreviewWidthMin
    readonly property int zoneSelectorPreviewWidthMax: settingsController.zoneSelectorPreviewWidthMax
    readonly property int zoneSelectorPreviewHeightMin: settingsController.zoneSelectorPreviewHeightMin
    readonly property int zoneSelectorPreviewHeightMax: settingsController.zoneSelectorPreviewHeightMax
    readonly property int zoneSelectorGridColumnsMax: settingsController.zoneSelectorGridColumnsMax
    readonly property int zoneSelectorTriggerMin: settingsController.zoneSelectorTriggerDistanceMin
    readonly property int zoneSelectorGridColumnsMin: settingsController.zoneSelectorGridColumnsMin
    readonly property int zoneSelectorMaxRowsMin: settingsController.zoneSelectorMaxRowsMin
    readonly property real screenAspectRatio: Screen.width > 0 && Screen.height > 0 ? (Screen.width / Screen.height) : (16 / 9)
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

        // =================================================================
        // PER-MONITOR SELECTION (shared between gaps and zone selector)
        // =================================================================
        MonitorSelectorSection {
            id: monitorSelector

            Layout.fillWidth: true
            appSettings: settingsController
            selectedScreenName: snappingHelper.selectedScreenName
            hasOverrides: snappingHelper.hasOverrides
            onSelectedScreenNameChanged: {
                snappingHelper.selectedScreenName = selectedScreenName;
                zoneSelectorSection.selectedScreenName = selectedScreenName;
            }
            onResetClicked: {
                snappingHelper.clearOverrides();
                zoneSelectorSection.resetOverrides();
            }
        }

        // =================================================================
        // GAPS
        // =================================================================
        Item {
            Layout.fillWidth: true
            implicitHeight: gapsCard.implicitHeight

            SettingsCard {
                id: gapsCard

                anchors.fill: parent
                headerText: i18n("Gaps")
                showAccent: true
                collapsible: true

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.smallSpacing

                    SettingsRow {
                        title: i18n("Zone padding")
                        description: i18n("Space between tiled windows")

                        SettingsSpinBox {
                            from: settingsController.gapMin
                            to: root.paddingMax
                            value: root.snappingSettingValue("ZonePadding", appSettings.zonePadding)
                            onValueModified: (value) => {
                                return root.writeSnappingSetting("ZonePadding", value, function(v) {
                                    appSettings.zonePadding = v;
                                });
                            }
                        }

                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        Layout.leftMargin: Kirigami.Units.largeSpacing
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                    }

                    SettingsRow {
                        visible: !perSideSwitch.checked
                        title: i18n("Edge gap")
                        description: i18n("Space from screen edges to tiled windows")

                        SpinBox {
                            id: outerGapSpinBox

                            from: 0
                            to: root.paddingMax
                            onValueModified: root.writeSnappingSetting("OuterGap", value, function(v) {
                                appSettings.outerGap = v;
                            })
                            Accessible.name: i18n("Edge gap")

                            Binding on value {
                                value: root.snappingSettingValue("OuterGap", appSettings.outerGap)
                                when: !outerGapSpinBox.activeFocus
                                restoreMode: Binding.RestoreNone
                            }

                        }

                    }

                    SettingsRow {
                        title: i18n("Per-side outer gaps")
                        description: perSideSwitch.checked ? i18n("Set different gap sizes for each screen edge") : i18n("Use a single outer gap value for all edges")

                        SettingsSwitch {
                            id: perSideSwitch

                            checked: root.snappingSettingValue("UsePerSideOuterGap", appSettings.usePerSideOuterGap)
                            accessibleName: i18n("Set gaps per side")
                            onToggled: root.writeSnappingSetting("UsePerSideOuterGap", checked, function(v) {
                                appSettings.usePerSideOuterGap = v;
                            })
                        }

                    }

                    // Per-side gap grid (only when per-side is enabled)
                    GridLayout {
                        visible: perSideSwitch.checked
                        Layout.alignment: Qt.AlignRight
                        Layout.rightMargin: Kirigami.Units.largeSpacing
                        columns: 4
                        columnSpacing: Kirigami.Units.largeSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Top")
                        }

                        SpinBox {
                            id: topGapSpinBox

                            from: 0
                            to: root.paddingMax
                            onValueModified: root.writeSnappingSetting("OuterGapTop", value, function(v) {
                                appSettings.outerGapTop = v;
                            })
                            Accessible.name: i18nc("@label", "Top edge gap")

                            Binding on value {
                                value: root.snappingSettingValue("OuterGapTop", appSettings.outerGapTop)
                                when: !topGapSpinBox.activeFocus
                                restoreMode: Binding.RestoreNone
                            }

                        }

                        Label {
                            text: i18n("Bottom")
                        }

                        SpinBox {
                            id: bottomGapSpinBox

                            from: 0
                            to: root.paddingMax
                            onValueModified: root.writeSnappingSetting("OuterGapBottom", value, function(v) {
                                appSettings.outerGapBottom = v;
                            })
                            Accessible.name: i18nc("@label", "Bottom edge gap")

                            Binding on value {
                                value: root.snappingSettingValue("OuterGapBottom", appSettings.outerGapBottom)
                                when: !bottomGapSpinBox.activeFocus
                                restoreMode: Binding.RestoreNone
                            }

                        }

                        Label {
                            text: i18n("Left")
                        }

                        SpinBox {
                            id: leftGapSpinBox

                            from: 0
                            to: root.paddingMax
                            onValueModified: root.writeSnappingSetting("OuterGapLeft", value, function(v) {
                                appSettings.outerGapLeft = v;
                            })
                            Accessible.name: i18nc("@label", "Left edge gap")

                            Binding on value {
                                value: root.snappingSettingValue("OuterGapLeft", appSettings.outerGapLeft)
                                when: !leftGapSpinBox.activeFocus
                                restoreMode: Binding.RestoreNone
                            }

                        }

                        Label {
                            text: i18n("Right")
                        }

                        SpinBox {
                            id: rightGapSpinBox

                            from: 0
                            to: root.paddingMax
                            onValueModified: root.writeSnappingSetting("OuterGapRight", value, function(v) {
                                appSettings.outerGapRight = v;
                            })
                            Accessible.name: i18nc("@label", "Right edge gap")

                            Binding on value {
                                value: root.snappingSettingValue("OuterGapRight", appSettings.outerGapRight)
                                when: !rightGapSpinBox.activeFocus
                                restoreMode: Binding.RestoreNone
                            }

                        }

                    }

                }

            }

        }

        // =================================================================
        // ZONE SELECTOR (per-monitor popup configuration)
        // =================================================================
        ZoneSelectorSection {
            id: zoneSelectorSection

            Layout.fillWidth: true
            appSettings: settingsController.settings
            controller: settingsController
            constants: root
            isCurrentTab: true
            screenAspectRatio: root.screenAspectRatio
            showMonitorSelector: false
        }

    }

}
