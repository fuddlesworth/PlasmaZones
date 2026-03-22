// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int algorithmPreviewWidth: Kirigami.Units.gridUnit * 18
    readonly property int algorithmPreviewHeight: Kirigami.Units.gridUnit * 10
    // Per-screen override helper
    property alias selectedScreenName: psHelper.selectedScreenName
    readonly property alias isPerScreen: psHelper.isPerScreen
    readonly property alias hasOverrides: psHelper.hasOverrides
    readonly property string effectiveAlgorithm: settingValue("Algorithm", appSettings.autotileAlgorithm)

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
        // Algorithm Card (per-monitor)
        // =================================================================
        SettingsCard {
            Layout.fillWidth: true
            headerText: i18n("Algorithm")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                // Live preview - centered at top
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.algorithmPreviewHeight + Kirigami.Units.gridUnit * 4

                    // Preview container
                    Item {
                        id: algorithmPreviewContainer

                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        width: root.algorithmPreviewWidth
                        height: root.algorithmPreviewHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Kirigami.Theme.backgroundColor
                            border.color: Kirigami.Theme.disabledTextColor
                            border.width: 1
                            radius: Kirigami.Units.smallSpacing

                            AlgorithmPreview {
                                anchors.fill: parent
                                anchors.margins: Kirigami.Units.smallSpacing
                                appSettings: settingsController
                                showLabel: false
                                algorithmId: root.effectiveAlgorithm
                                windowCount: previewWindowSlider.slider.value
                                splitRatio: root.effectiveAlgorithm === "centered-master" ? (root.settingValue("SplitRatio", appSettings.autotileCenteredMasterSplitRatio) || 0.5) : (root.settingValue("SplitRatio", appSettings.autotileSplitRatio) || 0.6)
                                masterCount: root.effectiveAlgorithm === "centered-master" ? (root.settingValue("MasterCount", appSettings.autotileCenteredMasterMasterCount) || 1) : (root.settingValue("MasterCount", appSettings.autotileMasterCount) || 1)
                            }

                        }

                        // Window count label below preview
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: Kirigami.Units.smallSpacing
                            text: i18np("Max %n window", "Max %n windows", previewWindowSlider.slider.value)
                            font: Kirigami.Theme.fixedWidthFont
                            opacity: 0.7
                        }

                    }

                }

                // Algorithm selection - centered
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Kirigami.Units.smallSpacing
                    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 25, parent.width)

                    WideComboBox {
                        id: algorithmCombo

                        Layout.alignment: Qt.AlignHCenter
                        Accessible.name: i18n("Tiling algorithm")
                        textRole: "name"
                        valueRole: "id"
                        model: settingsController.availableAlgorithms()
                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.effectiveAlgorithm))
                        onActivated: {
                            root.writeSetting("Algorithm", currentValue, function(v) {
                                appSettings.autotileAlgorithm = v;
                            });
                            // Reset max windows to the new algorithm's default
                            let algoData = model[currentIndex];
                            if (algoData && algoData.defaultMaxWindows !== undefined)
                                root.writeSetting("MaxWindows", algoData.defaultMaxWindows, function(v) {
                                appSettings.autotileMaxWindows = v;
                            });

                        }
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Select how windows are automatically arranged on screen")

                        // Re-sync when the effective algorithm changes externally
                        Connections {
                            function onEffectiveAlgorithmChanged() {
                                algorithmCombo.currentIndex = Math.max(0, algorithmCombo.indexOfValue(root.effectiveAlgorithm));
                            }

                            target: root
                        }

                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: {
                            const model = algorithmCombo.model;
                            const idx = algorithmCombo.currentIndex;
                            if (model && idx >= 0 && idx < model.length)
                                return model[idx].description || "";

                            return "";
                        }
                        wrapMode: Text.WordWrap
                        opacity: 0.7
                    }

                }

                // Max windows slider
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Kirigami.Units.smallSpacing
                    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 20, parent.width)

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: i18n("Max Windows")
                        font.weight: Font.DemiBold
                    }

                    SettingsSlider {
                        id: previewWindowSlider

                        Layout.fillWidth: true
                        from: 1
                        to: 12
                        stepSize: 1
                        formatValue: function(v) {
                            return Math.round(v).toString();
                        }
                        onMoved: (value) => {
                            return root.writeSetting("MaxWindows", Math.round(value), function(v) {
                                appSettings.autotileMaxWindows = v;
                            });
                        }
                    }

                    Binding {
                        target: previewWindowSlider.slider
                        property: "value"
                        value: root.settingValue("MaxWindows", appSettings.autotileMaxWindows)
                        when: !previewWindowSlider.slider.pressed
                        restoreMode: Binding.RestoreNone
                    }

                }

                // Algorithm-specific settings (master-stack, three-column, centered-master)
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 20, parent.width)
                    spacing: Kirigami.Units.smallSpacing
                    visible: root.effectiveAlgorithm === "master-stack" || root.effectiveAlgorithm === "three-column" || root.effectiveAlgorithm === "centered-master"

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                    }

                    // Master/Center ratio
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.effectiveAlgorithm === "three-column" || root.effectiveAlgorithm === "centered-master" ? i18n("Center Ratio") : i18n("Master Ratio")
                        font.weight: Font.DemiBold
                    }

                    SettingsSlider {
                        id: splitRatioSlider

                        Layout.fillWidth: true
                        from: 0.1
                        to: 0.9
                        stepSize: 0.05
                        formatValue: function(v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: (value) => {
                            if (root.effectiveAlgorithm === "centered-master")
                                root.writeSetting("SplitRatio", value, function(v) {
                                appSettings.autotileCenteredMasterSplitRatio = v;
                            });
                            else
                                root.writeSetting("SplitRatio", value, function(v) {
                                appSettings.autotileSplitRatio = v;
                            });
                        }
                    }

                    Binding {
                        target: splitRatioSlider.slider
                        property: "value"
                        value: root.effectiveAlgorithm === "centered-master" ? root.settingValue("SplitRatio", appSettings.autotileCenteredMasterSplitRatio) : root.settingValue("SplitRatio", appSettings.autotileSplitRatio)
                        when: !splitRatioSlider.slider.pressed
                        restoreMode: Binding.RestoreNone
                    }

                    // Master count - for master-stack and centered-master
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Kirigami.Units.smallSpacing
                        visible: root.effectiveAlgorithm === "master-stack" || root.effectiveAlgorithm === "centered-master"

                        Label {
                            text: root.effectiveAlgorithm === "centered-master" ? i18n("Center count:") : i18n("Master count:")
                        }

                        SpinBox {
                            id: masterCountSpinBox

                            from: 1
                            to: 5
                            onValueModified: {
                                if (root.effectiveAlgorithm === "centered-master")
                                    root.writeSetting("MasterCount", value, function(v) {
                                    appSettings.autotileCenteredMasterMasterCount = v;
                                });
                                else
                                    root.writeSetting("MasterCount", value, function(v) {
                                    appSettings.autotileMasterCount = v;
                                });
                            }
                            Accessible.name: root.effectiveAlgorithm === "centered-master" ? i18n("Center count") : i18n("Master count")
                            ToolTip.visible: hovered
                            ToolTip.text: root.effectiveAlgorithm === "centered-master" ? i18n("Number of windows in the center area") : i18n("Number of windows in the master area")

                            Binding on value {
                                value: root.effectiveAlgorithm === "centered-master" ? root.settingValue("MasterCount", appSettings.autotileCenteredMasterMasterCount) : root.settingValue("MasterCount", appSettings.autotileMasterCount)
                                when: !masterCountSpinBox.activeFocus
                                restoreMode: Binding.RestoreNone
                            }

                        }

                    }

                }

            }

        }

    }

}
