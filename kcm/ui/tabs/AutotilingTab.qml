// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import ".."
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Autotiling tab - Settings for automatic window tiling
 *
 * Provides configuration for autotiling algorithms, gaps, master area,
 * and behavior settings. Algorithm selection supports per-monitor overrides
 * when multiple monitors are detected.
 */
ScrollView {
    id: root

    required property var kcm
    required property QtObject constants
    // Whether this tab is currently visible (for conditional tooltips)
    property bool isCurrentTab: false
    // Per-screen override helper (applies to Algorithm card only)
    property alias selectedScreenName: psHelper.selectedScreenName
    readonly property alias isPerScreen: psHelper.isPerScreen
    readonly property alias hasOverrides: psHelper.hasOverrides
    // Convenience property for algorithm-specific visibility
    readonly property string effectiveAlgorithm: settingValue("Algorithm", kcm.autotileAlgorithm)

    signal requestAutotileBorderColorDialog()

    function settingValue(key, globalValue) {
        return psHelper.settingValue(key, globalValue);
    }

    function writeSetting(key, value, globalSetter) {
        psHelper.writeSetting(key, value, globalSetter);
    }

    clip: true
    contentWidth: availableWidth

    PerScreenOverrideHelper {
        id: psHelper

        kcm: root.kcm
        getterMethod: "getPerScreenAutotileSettings"
        setterMethod: "setPerScreenAutotileSetting"
        clearerMethod: "clearPerScreenAutotileSettings"
    }

    ColumnLayout {
        // ═══════════════════════════════════════════════════════════════════════
        // PER-MONITOR SETTINGS (Algorithm only)
        // ═══════════════════════════════════════════════════════════════════════

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // Enable toggle - prominent at top
        CheckBox {
            id: autotileEnabledCheck

            Layout.fillWidth: true
            text: i18n("Enable automatic window tiling")
            checked: kcm.autotileEnabled
            onToggled: kcm.autotileEnabled = checked
            font.bold: true
            Accessible.name: i18n("Enable autotiling")
        }

        // ═══════════════════════════════════════════════════════════════════════
        // BEHAVIOR CARD (global)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: behaviorCard.implicitHeight

            Kirigami.Card {
                id: behaviorCard

                anchors.fill: parent
                enabled: kcm.autotileEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Behavior")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    ComboBox {
                        id: insertPositionCombo

                        Kirigami.FormData.label: i18n("New windows:")
                        textRole: "text"
                        valueRole: "value"
                        model: [{
                            "text": i18n("Add to end of stack"),
                            "value": 0
                        }, {
                            "text": i18n("Insert after focused"),
                            "value": 1
                        }, {
                            "text": i18n("Add as master"),
                            "value": 2
                        }]
                        currentIndex: Math.max(0, indexOfValue(kcm.autotileInsertPosition))
                        onActivated: kcm.autotileInsertPosition = currentValue
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Focus:")
                        text: i18n("Automatically focus newly opened windows")
                        checked: kcm.autotileFocusNewWindows
                        onToggled: kcm.autotileFocusNewWindows = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: " "
                        text: i18n("Focus follows mouse pointer")
                        checked: kcm.autotileFocusFollowsMouse
                        onToggled: kcm.autotileFocusFollowsMouse = checked
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("When enabled, moving mouse over a window focuses it")
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Constraints:")
                        text: i18n("Respect window minimum size")
                        checked: kcm.autotileRespectMinimumSize
                        onToggled: kcm.autotileRespectMinimumSize = checked
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("Windows won't be resized smaller than their minimum. May cause layout to not fill screen.")
                    }

                    CheckBox {
                        id: hideTitleBarsCheck

                        Kirigami.FormData.label: i18n("Decorations:")
                        text: i18n("Hide title bars on tiled windows")
                        checked: kcm.autotileHideTitleBars
                        onToggled: kcm.autotileHideTitleBars = checked
                        ToolTip.visible: hovered && root.isCurrentTab
                        ToolTip.text: i18n("Remove window title bars while autotiled. Restored when floating or leaving autotile mode.")
                    }

                    // ── Border settings (visible when title bars hidden) ──
                    RowLayout {
                        Kirigami.FormData.label: i18n("Border width:")
                        visible: hideTitleBarsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: 10
                            value: kcm.autotileBorderWidth
                            onValueModified: kcm.autotileBorderWidth = value
                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Colored border drawn around borderless tiled windows (0 to disable)")
                        }

                        Label {
                            text: i18n("px")
                        }

                    }

                    CheckBox {
                        id: useSystemBorderColorsCheck

                        Kirigami.FormData.label: i18n("Color scheme:")
                        visible: hideTitleBarsCheck.checked
                        text: i18n("Use system accent color")
                        checked: kcm.autotileUseSystemBorderColors
                        onToggled: kcm.autotileUseSystemBorderColors = checked
                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Border:")
                        visible: hideTitleBarsCheck.checked && !useSystemBorderColorsCheck.checked
                        spacing: Kirigami.Units.smallSpacing

                        ColorButton {
                            color: kcm.autotileBorderColor
                            onClicked: root.requestAutotileBorderColorDialog()
                        }

                        Label {
                            text: kcm.autotileBorderColor.toString().toUpperCase()
                            font: Kirigami.Theme.fixedWidthFont
                        }

                    }

                }

            }

        }

        MonitorSelectorSection {
            Layout.fillWidth: true
            kcm: root.kcm
            featureEnabled: kcm.autotileEnabled
            selectedScreenName: root.selectedScreenName
            hasOverrides: root.hasOverrides
            onSelectedScreenNameChanged: root.selectedScreenName = selectedScreenName
            onResetClicked: psHelper.clearOverrides()
        }

        // ═══════════════════════════════════════════════════════════════════════
        // GAPS CARD (per-monitor)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: gapsCard.implicitHeight

            Kirigami.Card {
                id: gapsCard

                anchors.fill: parent
                enabled: kcm.autotileEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Gaps")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    RowLayout {
                        Kirigami.FormData.label: i18n("Inner gap:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.constants.autotileGapMax
                            value: root.settingValue("InnerGap", kcm.autotileInnerGap)
                            onValueModified: root.writeSetting("InnerGap", value, function(v) {
                                kcm.autotileInnerGap = v;
                            })
                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Gap between tiled windows")
                        }

                        Label {
                            text: i18n("px")
                        }

                    }

                    RowLayout {
                        Kirigami.FormData.label: i18n("Outer gap:")
                        spacing: Kirigami.Units.smallSpacing

                        SpinBox {
                            from: 0
                            to: root.constants.autotileGapMax
                            value: root.settingValue("OuterGap", kcm.autotileOuterGap)
                            enabled: !autotilePerSideCheck.checked
                            onValueModified: root.writeSetting("OuterGap", value, function(v) {
                                kcm.autotileOuterGap = v;
                            })
                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Gap from screen edges")
                        }

                        Label {
                            text: i18n("px")
                            visible: !autotilePerSideCheck.checked
                        }

                        CheckBox {
                            id: autotilePerSideCheck

                            text: i18n("Set per side")
                            checked: root.settingValue("UsePerSideOuterGap", kcm.autotileUsePerSideOuterGap)
                            onToggled: root.writeSetting("UsePerSideOuterGap", checked, function(v) {
                                kcm.autotileUsePerSideOuterGap = v;
                            })
                        }

                    }

                    GridLayout {
                        Kirigami.FormData.label: i18n("Per-side gaps:")
                        visible: autotilePerSideCheck.checked
                        columns: 6
                        columnSpacing: Kirigami.Units.smallSpacing
                        rowSpacing: Kirigami.Units.smallSpacing

                        Label {
                            text: i18n("Top:")
                        }

                        SpinBox {
                            from: 0
                            to: root.constants.autotileGapMax
                            value: root.settingValue("OuterGapTop", kcm.autotileOuterGapTop)
                            onValueModified: root.writeSetting("OuterGapTop", value, function(v) {
                                kcm.autotileOuterGapTop = v;
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
                            to: root.constants.autotileGapMax
                            value: root.settingValue("OuterGapBottom", kcm.autotileOuterGapBottom)
                            onValueModified: root.writeSetting("OuterGapBottom", value, function(v) {
                                kcm.autotileOuterGapBottom = v;
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
                            to: root.constants.autotileGapMax
                            value: root.settingValue("OuterGapLeft", kcm.autotileOuterGapLeft)
                            onValueModified: root.writeSetting("OuterGapLeft", value, function(v) {
                                kcm.autotileOuterGapLeft = v;
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
                            to: root.constants.autotileGapMax
                            value: root.settingValue("OuterGapRight", kcm.autotileOuterGapRight)
                            onValueModified: root.writeSetting("OuterGapRight", value, function(v) {
                                kcm.autotileOuterGapRight = v;
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
                        checked: root.settingValue("SmartGaps", kcm.autotileSmartGaps)
                        onToggled: root.writeSetting("SmartGaps", checked, function(v) {
                            kcm.autotileSmartGaps = v;
                        })
                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // ALGORITHM CARD (per-monitor)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: algorithmCard.implicitHeight

            Kirigami.Card {
                id: algorithmCard

                anchors.fill: parent
                enabled: kcm.autotileEnabled

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Algorithm")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Live preview - centered at top
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.constants.algorithmPreviewHeight + Kirigami.Units.gridUnit * 4

                        // Preview container
                        Item {
                            id: algorithmPreviewContainer

                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            width: root.constants.algorithmPreviewWidth
                            height: root.constants.algorithmPreviewHeight

                            Rectangle {
                                anchors.fill: parent
                                color: Kirigami.Theme.backgroundColor
                                border.color: Kirigami.Theme.disabledTextColor
                                border.width: 1
                                radius: Kirigami.Units.smallSpacing

                                AlgorithmPreview {
                                    anchors.fill: parent
                                    anchors.margins: Kirigami.Units.smallSpacing
                                    kcm: root.kcm
                                    showLabel: false
                                    algorithmId: root.effectiveAlgorithm
                                    windowCount: previewWindowSlider.value
                                    splitRatio: root.effectiveAlgorithm === "centered-master" ? root.settingValue("SplitRatio", root.kcm.autotileCenteredMasterSplitRatio) : root.settingValue("SplitRatio", root.kcm.autotileSplitRatio)
                                    masterCount: root.effectiveAlgorithm === "centered-master" ? root.settingValue("MasterCount", root.kcm.autotileCenteredMasterMasterCount) : root.settingValue("MasterCount", root.kcm.autotileMasterCount)
                                }

                            }

                            // Window count label below preview
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.bottom
                                anchors.topMargin: Kirigami.Units.smallSpacing
                                text: i18np("Max %1 window", "Max %1 windows", previewWindowSlider.value)
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

                        ComboBox {
                            id: algorithmCombo

                            Layout.alignment: Qt.AlignHCenter
                            Layout.preferredWidth: Kirigami.Units.gridUnit * 15
                            Accessible.name: i18n("Tiling algorithm")
                            textRole: "name"
                            valueRole: "id"
                            model: kcm.availableAlgorithms()
                            // indexOfValue is unreliable during initial model population;
                            // defer to Component.onCompleted so the model is fully ready.
                            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.effectiveAlgorithm))
                            onActivated: {
                                root.writeSetting("Algorithm", currentValue, function(v) {
                                    kcm.autotileAlgorithm = v;
                                });
                                // Reset max windows to the new algorithm's default
                                let algoData = model[currentIndex];
                                if (algoData && algoData.defaultMaxWindows !== undefined)
                                    root.writeSetting("MaxWindows", algoData.defaultMaxWindows, function(v) {
                                    kcm.autotileMaxWindows = v;
                                });

                            }
                            ToolTip.visible: hovered && root.isCurrentTab
                            ToolTip.text: i18n("Select how windows are automatically arranged on screen")

                            // Re-sync when the effective algorithm changes externally
                            // (e.g. per-screen override selection)
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

                    // Max windows slider - controls preview, zone count, and persisted setting
                    ColumnLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: Kirigami.Units.smallSpacing
                        Layout.maximumWidth: Math.min(Kirigami.Units.gridUnit * 20, parent.width)

                        Label {
                            Layout.alignment: Qt.AlignHCenter
                            text: i18n("Max Windows")
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: previewWindowSlider

                                Layout.fillWidth: true
                                Accessible.name: i18n("Maximum windows preview")
                                from: 1
                                to: 12
                                stepSize: 1
                                onMoved: root.writeSetting("MaxWindows", Math.round(value), function(v) {
                                    kcm.autotileMaxWindows = v;
                                })
                                ToolTip.visible: hovered && root.isCurrentTab
                                ToolTip.text: i18n("Maximum number of windows to tile with this algorithm")

                                Binding on value {
                                    value: root.settingValue("MaxWindows", kcm.autotileMaxWindows)
                                    when: !previewWindowSlider.pressed
                                    restoreMode: Binding.RestoreNone
                                }

                            }

                            Label {
                                text: Math.round(previewWindowSlider.value)
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth
                                horizontalAlignment: Text.AlignRight
                                font: Kirigami.Theme.fixedWidthFont
                            }

                        }

                    }

                    // ─────────────────────────────────────────────────────────────
                    // Algorithm-specific settings (master-stack, three-column, centered-master)
                    // ─────────────────────────────────────────────────────────────
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
                            font.bold: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: splitRatioSlider

                                Layout.fillWidth: true
                                from: 0.1
                                to: 0.9
                                stepSize: 0.05
                                onMoved: {
                                    if (root.effectiveAlgorithm === "centered-master")
                                        root.writeSetting("SplitRatio", value, function(v) {
                                        kcm.autotileCenteredMasterSplitRatio = v;
                                    });
                                    else
                                        root.writeSetting("SplitRatio", value, function(v) {
                                        kcm.autotileSplitRatio = v;
                                    });
                                }
                                ToolTip.visible: hovered && root.isCurrentTab
                                ToolTip.text: root.effectiveAlgorithm === "three-column" || root.effectiveAlgorithm === "centered-master" ? i18n("Proportion of screen width for the center column") : i18n("Proportion of screen width for the master area")

                                Binding on value {
                                    value: root.effectiveAlgorithm === "centered-master" ? root.settingValue("SplitRatio", kcm.autotileCenteredMasterSplitRatio) : root.settingValue("SplitRatio", kcm.autotileSplitRatio)
                                    when: !splitRatioSlider.pressed
                                    restoreMode: Binding.RestoreNone
                                }

                            }

                            Label {
                                text: Math.round(splitRatioSlider.value * 100) + "%"
                                Layout.preferredWidth: root.constants.sliderValueLabelWidth
                                horizontalAlignment: Text.AlignRight
                                font: Kirigami.Theme.fixedWidthFont
                            }

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
                                        kcm.autotileCenteredMasterMasterCount = v;
                                    });
                                    else
                                        root.writeSetting("MasterCount", value, function(v) {
                                        kcm.autotileMasterCount = v;
                                    });
                                }
                                ToolTip.visible: hovered && root.isCurrentTab
                                ToolTip.text: root.effectiveAlgorithm === "centered-master" ? i18n("Number of windows in the center area") : i18n("Number of windows in the master area")

                                Binding on value {
                                    value: root.effectiveAlgorithm === "centered-master" ? root.settingValue("MasterCount", kcm.autotileCenteredMasterMasterCount) : root.settingValue("MasterCount", kcm.autotileMasterCount)
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

}
