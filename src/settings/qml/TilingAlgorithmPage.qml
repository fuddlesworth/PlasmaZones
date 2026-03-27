// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    readonly property int gapMax: settingsController.autotileGapMax
    readonly property int algorithmPreviewWidth: Kirigami.Units.gridUnit * 18
    readonly property int algorithmPreviewHeight: Kirigami.Units.gridUnit * 10
    // Per-screen override helper
    property alias selectedScreenName: psHelper.selectedScreenName
    readonly property alias isPerScreen: psHelper.isPerScreen
    readonly property alias hasOverrides: psHelper.hasOverrides
    readonly property string effectiveAlgorithm: settingValue("Algorithm", appSettings.autotileAlgorithm)
    // Derive algorithm ID from the combo's current selection (tracks UI immediately,
    // not delayed by D-Bus round-trip to daemon)
    readonly property string selectedAlgorithm: {
        if (!algorithmCombo)
            return root.effectiveAlgorithm;

        let val = algorithmCombo.currentValue;
        if (!val || val === "")
            return root.effectiveAlgorithm;

        if (val.startsWith("autotile:"))
            return val.substring(9);

        return val;
    }
    // Data-driven algorithm capabilities (lookup from availableAlgorithms by ID)
    readonly property var algoCapabilities: {
        const algos = settingsController.availableAlgorithms();
        const algoId = root.selectedAlgorithm;
        for (let i = 0; i < algos.length; i++) {
            if (algos[i].id === algoId)
                return algos[i];

        }
        return null;
    }
    readonly property bool algoSupportsSplitRatio: algoCapabilities ? (algoCapabilities.supportsSplitRatio === true) : false
    readonly property bool algoSupportsMasterCount: algoCapabilities ? (algoCapabilities.supportsMasterCount === true) : false
    // Whether the algorithm uses a center layout (ratio/count labels say "Center" instead of "Master").
    // Check capabilities map first (for future extensibility via scripted algorithm metadata),
    // falling back to hardcoded IDs for built-in algorithms. See PR #256 / M13.
    readonly property bool algoCenterLayout: algoCapabilities ? (algoCapabilities.centerLayout === true) : (root.selectedAlgorithm === "three-column" || root.selectedAlgorithm === "centered-master")

    function savedAlgoSetting(key, fallback) {
        // For the active algorithm, prefer the live global setting over the
        // per-algorithm map (which is only updated on algorithm switch).
        if (root.selectedAlgorithm === root.effectiveAlgorithm) {
            if (key === "splitRatio")
                return root.settingValue("SplitRatio", appSettings.autotileSplitRatio);

            if (key === "masterCount")
                return root.settingValue("MasterCount", appSettings.autotileMasterCount);

        }
        var perAlgo = appSettings.autotilePerAlgorithmSettings;
        var entry = perAlgo ? perAlgo[root.selectedAlgorithm] : null;
        return (entry && entry[key] !== undefined) ? entry[key] : fallback;
    }

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
            selectedScreenName: root.selectedScreenName
            hasOverrides: root.hasOverrides
            onSelectedScreenNameChanged: root.selectedScreenName = selectedScreenName
            onResetClicked: psHelper.clearOverrides()
        }

        // =================================================================
        // Gaps Card (per-monitor)
        // =================================================================
        GapsSettingsCard {
            Layout.fillWidth: true
            gapMax: root.gapMax
            gapMin: settingsController.autotileGapMin
            innerGapValue: root.settingValue("InnerGap", appSettings.autotileInnerGap)
            outerGapValue: root.settingValue("OuterGap", appSettings.autotileOuterGap)
            usePerSideOuterGap: root.settingValue("UsePerSideOuterGap", appSettings.autotileUsePerSideOuterGap)
            smartGapsValue: root.settingValue("SmartGaps", appSettings.autotileSmartGaps)
            outerGapTopValue: root.settingValue("OuterGapTop", appSettings.autotileOuterGapTop)
            outerGapBottomValue: root.settingValue("OuterGapBottom", appSettings.autotileOuterGapBottom)
            outerGapLeftValue: root.settingValue("OuterGapLeft", appSettings.autotileOuterGapLeft)
            outerGapRightValue: root.settingValue("OuterGapRight", appSettings.autotileOuterGapRight)
            onInnerGapModified: (value) => {
                return root.writeSetting("InnerGap", value, function(v) {
                    appSettings.autotileInnerGap = v;
                });
            }
            onOuterGapModified: (value) => {
                return root.writeSetting("OuterGap", value, function(v) {
                    appSettings.autotileOuterGap = v;
                });
            }
            onUsePerSideOuterGapToggled: (checked) => {
                return root.writeSetting("UsePerSideOuterGap", checked, function(v) {
                    appSettings.autotileUsePerSideOuterGap = v;
                });
            }
            onOuterGapTopModified: (value) => {
                return root.writeSetting("OuterGapTop", value, function(v) {
                    appSettings.autotileOuterGapTop = v;
                });
            }
            onOuterGapBottomModified: (value) => {
                return root.writeSetting("OuterGapBottom", value, function(v) {
                    appSettings.autotileOuterGapBottom = v;
                });
            }
            onOuterGapLeftModified: (value) => {
                return root.writeSetting("OuterGapLeft", value, function(v) {
                    appSettings.autotileOuterGapLeft = v;
                });
            }
            onOuterGapRightModified: (value) => {
                return root.writeSetting("OuterGapRight", value, function(v) {
                    appSettings.autotileOuterGapRight = v;
                });
            }
            onSmartGapsToggled: (checked) => {
                return root.writeSetting("SmartGaps", checked, function(v) {
                    appSettings.autotileSmartGaps = v;
                });
            }
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
                                algorithmId: root.selectedAlgorithm
                                algorithmName: root.algoCapabilities ? (root.algoCapabilities.name || "") : ""
                                windowCount: previewWindowSlider.slider.value
                                splitRatio: {
                                    // When user is dragging the slider, use live value
                                    if (splitRatioSlider.slider.pressed)
                                        return splitRatioSlider.slider.value;

                                    return root.savedAlgoSetting("splitRatio", root.algoCapabilities ? root.algoCapabilities.defaultSplitRatio : 0.6);
                                }
                                masterCount: root.savedAlgoSetting("masterCount", 1)
                                zoneNumberDisplay: root.algoCapabilities ? (root.algoCapabilities.zoneNumberDisplay || "all") : "all"
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

                // Algorithm selection - uses LayoutComboBox with preview thumbnails
                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Kirigami.Units.smallSpacing
                    Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 25, parent.width)

                    LayoutComboBox {
                        // "Default" selected — use default algorithm

                        id: algorithmCombo

                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        Accessible.name: i18n("Tiling algorithm")
                        appSettings: settingsController
                        showPreview: true
                        layoutFilter: 1 // Autotile algorithms only
                        showNoneOption: false
                        currentLayoutId: "autotile:" + root.effectiveAlgorithm
                        onActivated: {
                            // Extract algorithm ID from autotile: prefixed value
                            let selectedId = algorithmCombo.currentValue;
                            if (selectedId === "")
                                selectedId = settingsController.autotileAlgorithm;
                            else if (selectedId.startsWith("autotile:"))
                                selectedId = selectedId.substring(9);
                            root.writeSetting("Algorithm", selectedId, function(v) {
                                appSettings.autotileAlgorithm = v;
                            });
                            // Reset maxWindows to the new algorithm's default
                            if (root.algoCapabilities) {
                                var newDefault = root.algoCapabilities.defaultMaxWindows || 6;
                                if (previewWindowSlider) {
                                    previewWindowSlider.slider.value = newDefault;
                                    root.writeSetting("MaxWindows", newDefault, function(v) {
                                        appSettings.autotileMaxWindows = v;
                                    });
                                }
                            }
                        }
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
                        from: settingsController.autotileMaxWindowsMin
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
                    visible: root.algoSupportsSplitRatio || root.algoSupportsMasterCount

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        Layout.topMargin: Kirigami.Units.smallSpacing
                    }

                    // Master/Center ratio
                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.algoCenterLayout ? i18n("Center Ratio") : i18n("Master Ratio")
                        font.weight: Font.DemiBold
                    }

                    SettingsSlider {
                        id: splitRatioSlider

                        Layout.fillWidth: true
                        from: settingsController.autotileSplitRatioMin
                        to: 0.9
                        stepSize: 0.05
                        formatValue: function(v) {
                            return Math.round(v * 100) + "%";
                        }
                        onMoved: (value) => {
                            // Write to the global split ratio — the engine's
                            // setAlgorithm() save/restore logic persists it
                            // into the per-algorithm map on algorithm switch.
                            root.writeSetting("SplitRatio", value, function(v) {
                                appSettings.autotileSplitRatio = v;
                            });
                        }
                    }

                    Binding {
                        target: splitRatioSlider.slider
                        property: "value"
                        value: root.savedAlgoSetting("splitRatio", root.algoCapabilities ? root.algoCapabilities.defaultSplitRatio : 0.6)
                        when: !splitRatioSlider.slider.pressed
                        restoreMode: Binding.RestoreNone
                    }

                    // Master count - for master-stack and centered-master
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Kirigami.Units.smallSpacing
                        visible: root.algoSupportsMasterCount

                        Label {
                            text: root.algoCenterLayout ? i18n("Center count:") : i18n("Master count:")
                        }

                        SpinBox {
                            id: masterCountSpinBox

                            from: settingsController.autotileMasterCountMin
                            to: 5
                            onValueModified: {
                                root.writeSetting("MasterCount", value, function(v) {
                                    appSettings.autotileMasterCount = v;
                                });
                            }
                            Accessible.name: root.algoCenterLayout ? i18n("Center count") : i18n("Master count")
                            ToolTip.visible: hovered
                            ToolTip.text: root.algoCenterLayout ? i18n("Number of windows in the center area") : i18n("Number of windows in the master area")

                            Binding on value {
                                value: root.savedAlgoSetting("masterCount", 1)
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
