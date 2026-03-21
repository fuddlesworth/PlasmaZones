// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

Flickable {
    id: root

    // Layout constants
    readonly property int gapMax: 50
    readonly property int algorithmPreviewWidth: 280
    readonly property int algorithmPreviewHeight: 160
    readonly property int sliderValueLabelWidth: 40
    readonly property bool bordersActive: hideTitleBarsCheck.checked || showBorderCheck.checked
    // Per-screen override helper (applies to Algorithm + Gaps cards)
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

        // =====================================================================
        // Enable toggle
        // =====================================================================
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.largeSpacing

            Label {
                text: i18n("Enable Automatic Tiling")
                font.bold: true
            }

            Item {
                Layout.fillWidth: true
            }

            Switch {
                checked: appSettings.autotileEnabled
                onToggled: appSettings.autotileEnabled = checked
                Accessible.name: i18n("Enable automatic tiling")
            }

        }

        // =====================================================================
        // Appearance Card (Borders + Colors, matching original structure)
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: appSettings.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Appearance")
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Colors")
                }

                CheckBox {
                    id: useSystemColorsCheck

                    Kirigami.FormData.label: i18n("Color scheme:")
                    text: i18n("Use system accent color")
                    checked: appSettings.autotileUseSystemBorderColors
                    onToggled: appSettings.autotileUseSystemBorderColors = checked
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Active color:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    ColorButton {
                        color: appSettings.autotileBorderColor
                        onClicked: {
                            activeBorderColorDialog.selectedColor = appSettings.autotileBorderColor;
                            activeBorderColorDialog.open();
                        }
                    }

                    Label {
                        text: appSettings.autotileBorderColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Inactive color:")
                    visible: !useSystemColorsCheck.checked
                    spacing: Kirigami.Units.smallSpacing

                    ColorButton {
                        color: appSettings.autotileInactiveBorderColor
                        onClicked: {
                            inactiveBorderColorDialog.selectedColor = appSettings.autotileInactiveBorderColor;
                            inactiveBorderColorDialog.open();
                        }
                    }

                    Label {
                        text: appSettings.autotileInactiveBorderColor.toString().toUpperCase()
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Decorations")
                }

                CheckBox {
                    id: hideTitleBarsCheck

                    Kirigami.FormData.label: i18n("Title bars:")
                    text: i18n("Hide title bars on tiled windows")
                    checked: appSettings.autotileHideTitleBars
                    onToggled: appSettings.autotileHideTitleBars = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Remove window title bars while autotiled. Restored when floating or leaving autotile mode.")
                }

                Kirigami.Separator {
                    Kirigami.FormData.isSection: true
                    Kirigami.FormData.label: i18n("Borders")
                }

                CheckBox {
                    id: showBorderCheck

                    Kirigami.FormData.label: i18n("Border:")
                    text: i18n("Show borders in tiling mode")
                    checked: appSettings.autotileShowBorder
                    onToggled: appSettings.autotileShowBorder = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Draw colored borders around all windows in tiling mode. Active color for focused, inactive for unfocused. Works with or without hidden title bars.")
                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Width:")
                    visible: root.bordersActive
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: 10
                        value: appSettings.autotileBorderWidth
                        onValueModified: appSettings.autotileBorderWidth = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Colored border drawn around tiled windows (0 to disable)")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

                RowLayout {
                    Kirigami.FormData.label: i18n("Corner radius:")
                    visible: root.bordersActive
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: 20
                        value: appSettings.autotileBorderRadius
                        onValueModified: appSettings.autotileBorderRadius = value
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Corner radius for the border (0 for square corners)")
                    }

                    Label {
                        text: i18n("px")
                    }

                }

            }

        }

        // =====================================================================
        // Behavior Card
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: appSettings.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Behavior")
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                ComboBox {
                    Kirigami.FormData.label: i18n("New windows:")
                    Layout.fillWidth: true
                    textRole: "text"
                    valueRole: "value"
                    model: [{
                        "text": i18n("Add after existing windows"),
                        "value": 0
                    }, {
                        "text": i18n("Insert after focused"),
                        "value": 1
                    }, {
                        "text": i18n("Add as main window"),
                        "value": 2
                    }]
                    currentIndex: Math.max(0, indexOfValue(appSettings.autotileInsertPosition))
                    onActivated: appSettings.autotileInsertPosition = currentValue
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Focus:")
                    text: i18n("Automatically focus newly opened windows")
                    checked: appSettings.autotileFocusNewWindows
                    onToggled: appSettings.autotileFocusNewWindows = checked
                }

                CheckBox {
                    Kirigami.FormData.label: " "
                    text: i18n("Focus follows mouse pointer")
                    checked: appSettings.autotileFocusFollowsMouse
                    onToggled: appSettings.autotileFocusFollowsMouse = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("When enabled, moving mouse over a window focuses it")
                }

                CheckBox {
                    Kirigami.FormData.label: i18n("Constraints:")
                    text: i18n("Respect window minimum size")
                    checked: appSettings.autotileRespectMinimumSize
                    onToggled: appSettings.autotileRespectMinimumSize = checked
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Windows will not be resized below their minimum size. This may leave gaps in the layout.")
                }

            }

        }

        // =====================================================================
        // Monitor Selector (per-screen overrides for Algorithm + Gaps)
        // =====================================================================
        MonitorSelectorSection {
            Layout.fillWidth: true
            appSettings: settingsController
            featureEnabled: settingsController.settings.autotileEnabled
            selectedScreenName: root.selectedScreenName
            hasOverrides: root.hasOverrides
            onSelectedScreenNameChanged: root.selectedScreenName = selectedScreenName
            onResetClicked: psHelper.clearOverrides()
        }

        // =====================================================================
        // Gaps Card (per-monitor)
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: appSettings.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Gaps")
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

            contentItem: Kirigami.FormLayout {
                RowLayout {
                    Kirigami.FormData.label: i18n("Inner gap:")
                    spacing: Kirigami.Units.smallSpacing

                    SpinBox {
                        from: 0
                        to: root.gapMax
                        value: root.settingValue("InnerGap", appSettings.autotileInnerGap)
                        onValueModified: root.writeSetting("InnerGap", value, function(v) {
                            appSettings.autotileInnerGap = v;
                        })
                        ToolTip.visible: hovered
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
                        to: root.gapMax
                        value: root.settingValue("OuterGap", appSettings.autotileOuterGap)
                        enabled: !perSideCheck.checked
                        onValueModified: root.writeSetting("OuterGap", value, function(v) {
                            appSettings.autotileOuterGap = v;
                        })
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Gap from screen edges")
                    }

                    Label {
                        text: i18n("px")
                        visible: !perSideCheck.checked
                    }

                    CheckBox {
                        id: perSideCheck

                        text: i18n("Set per side")
                        checked: root.settingValue("UsePerSideOuterGap", appSettings.autotileUsePerSideOuterGap)
                        onToggled: root.writeSetting("UsePerSideOuterGap", checked, function(v) {
                            appSettings.autotileUsePerSideOuterGap = v;
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

        // =====================================================================
        // Algorithm Card (per-monitor)
        // =====================================================================
        Kirigami.Card {
            Layout.fillWidth: true
            enabled: appSettings.autotileEnabled

            header: Kirigami.Heading {
                text: i18n("Algorithm")
                level: 3
                padding: Kirigami.Units.smallSpacing
            }

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
                                windowCount: previewWindowSlider.value
                                splitRatio: root.effectiveAlgorithm === "centered-master" ? (root.settingValue("SplitRatio", appSettings.autotileCenteredMasterSplitRatio) || 0.5) : (root.settingValue("SplitRatio", appSettings.autotileSplitRatio) || 0.6)
                                masterCount: root.effectiveAlgorithm === "centered-master" ? (root.settingValue("MasterCount", appSettings.autotileCenteredMasterMasterCount) || 1) : (root.settingValue("MasterCount", appSettings.autotileMasterCount) || 1)
                            }

                        }

                        // Window count label below preview
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: Kirigami.Units.smallSpacing
                            text: i18np("Max %n window", "Max %n windows", previewWindowSlider.value)
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
                        // indexOfValue is unreliable during initial model population;
                        // defer to Component.onCompleted so the model is fully ready.
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
                                appSettings.autotileMaxWindows = v;
                            })
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Maximum number of windows to tile with this algorithm")

                            Binding on value {
                                value: root.settingValue("MaxWindows", appSettings.autotileMaxWindows)
                                when: !previewWindowSlider.pressed
                                restoreMode: Binding.RestoreNone
                            }

                        }

                        Label {
                            text: Math.round(previewWindowSlider.value)
                            Layout.preferredWidth: root.sliderValueLabelWidth
                            horizontalAlignment: Text.AlignRight
                            font: Kirigami.Theme.fixedWidthFont
                        }

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
                                    appSettings.autotileCenteredMasterSplitRatio = v;
                                });
                                else
                                    root.writeSetting("SplitRatio", value, function(v) {
                                    appSettings.autotileSplitRatio = v;
                                });
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: root.effectiveAlgorithm === "three-column" || root.effectiveAlgorithm === "centered-master" ? i18n("Proportion of screen width for the center column") : i18n("Proportion of screen width for the master area")

                            Binding on value {
                                value: root.effectiveAlgorithm === "centered-master" ? root.settingValue("SplitRatio", appSettings.autotileCenteredMasterSplitRatio) : root.settingValue("SplitRatio", appSettings.autotileSplitRatio)
                                when: !splitRatioSlider.pressed
                                restoreMode: Binding.RestoreNone
                            }

                        }

                        Label {
                            text: Math.round(splitRatioSlider.value * 100) + "%"
                            Layout.preferredWidth: root.sliderValueLabelWidth
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

    // =========================================================================
    // Color Dialogs
    // =========================================================================
    ColorDialog {
        id: activeBorderColorDialog

        title: i18n("Choose Active Border Color")
        onAccepted: appSettings.autotileBorderColor = selectedColor
    }

    ColorDialog {
        id: inactiveBorderColorDialog

        title: i18n("Choose Inactive Border Color")
        onAccepted: appSettings.autotileInactiveBorderColor = selectedColor
    }

}
