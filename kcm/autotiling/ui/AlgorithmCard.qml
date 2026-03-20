// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Algorithm card for the Autotiling sub-KCM.
 *
 * Contains the live preview, algorithm selector, max windows slider,
 * and algorithm-specific settings (split ratio, master count).
 *
 * Required properties:
 *   - kcm: the KCM backend object
 *   - constants: root object providing algorithmPreviewWidth, algorithmPreviewHeight,
 *                sliderValueLabelWidth, effectiveAlgorithm, settingValue(), writeSetting()
 */
Item {
    id: algoRoot

    required property var kcm
    required property var constants

    Layout.fillWidth: true
    implicitHeight: algorithmCard.implicitHeight

    Kirigami.Card {
        id: algorithmCard

        anchors.fill: parent
        enabled: algoRoot.kcm.autotileEnabled

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
                Layout.preferredHeight: algoRoot.constants.algorithmPreviewHeight + Kirigami.Units.gridUnit * 4

                // Preview container
                Item {
                    id: algorithmPreviewContainer

                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    width: algoRoot.constants.algorithmPreviewWidth
                    height: algoRoot.constants.algorithmPreviewHeight

                    Rectangle {
                        anchors.fill: parent
                        color: Kirigami.Theme.backgroundColor
                        border.color: Kirigami.Theme.disabledTextColor
                        border.width: 1
                        radius: Kirigami.Units.smallSpacing

                        AlgorithmPreview {
                            anchors.fill: parent
                            anchors.margins: Kirigami.Units.smallSpacing
                            kcm: algoRoot.kcm
                            showLabel: false
                            algorithmId: algoRoot.constants.effectiveAlgorithm
                            windowCount: previewWindowSlider.value
                            splitRatio: algoRoot.constants.effectiveAlgorithm === "centered-master" ? algoRoot.constants.settingValue("SplitRatio", algoRoot.kcm.autotileCenteredMasterSplitRatio) : algoRoot.constants.settingValue("SplitRatio", algoRoot.kcm.autotileSplitRatio)
                            masterCount: algoRoot.constants.effectiveAlgorithm === "centered-master" ? algoRoot.constants.settingValue("MasterCount", algoRoot.kcm.autotileCenteredMasterMasterCount) : algoRoot.constants.settingValue("MasterCount", algoRoot.kcm.autotileMasterCount)
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
                    model: algoRoot.kcm.availableAlgorithms()
                    // indexOfValue is unreliable during initial model population;
                    // defer to Component.onCompleted so the model is fully ready.
                    Component.onCompleted: currentIndex = Math.max(0, indexOfValue(algoRoot.constants.effectiveAlgorithm))
                    onActivated: {
                        algoRoot.constants.writeSetting("Algorithm", currentValue, function(v) {
                            algoRoot.kcm.autotileAlgorithm = v;
                        });
                        // Reset max windows to the new algorithm's default
                        let algoData = model[currentIndex];
                        if (algoData && algoData.defaultMaxWindows !== undefined)
                            algoRoot.constants.writeSetting("MaxWindows", algoData.defaultMaxWindows, function(v) {
                            algoRoot.kcm.autotileMaxWindows = v;
                        });

                    }
                    ToolTip.visible: hovered
                    ToolTip.text: i18n("Select how windows are automatically arranged on screen")

                    // Re-sync when the effective algorithm changes externally
                    // (e.g. per-screen override selection)
                    Connections {
                        function onEffectiveAlgorithmChanged() {
                            algorithmCombo.currentIndex = Math.max(0, algorithmCombo.indexOfValue(algoRoot.constants.effectiveAlgorithm));
                        }

                        target: algoRoot.constants
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
                        onMoved: algoRoot.constants.writeSetting("MaxWindows", Math.round(value), function(v) {
                            algoRoot.kcm.autotileMaxWindows = v;
                        })
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Maximum number of windows to tile with this algorithm")

                        Binding on value {
                            value: algoRoot.constants.settingValue("MaxWindows", algoRoot.kcm.autotileMaxWindows)
                            when: !previewWindowSlider.pressed
                            restoreMode: Binding.RestoreNone
                        }

                    }

                    Label {
                        text: Math.round(previewWindowSlider.value)
                        Layout.preferredWidth: algoRoot.constants.sliderValueLabelWidth
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
                visible: algoRoot.constants.effectiveAlgorithm === "master-stack" || algoRoot.constants.effectiveAlgorithm === "three-column" || algoRoot.constants.effectiveAlgorithm === "centered-master"

                Kirigami.Separator {
                    Layout.fillWidth: true
                    Layout.topMargin: Kirigami.Units.smallSpacing
                }

                // Master/Center ratio
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: algoRoot.constants.effectiveAlgorithm === "three-column" || algoRoot.constants.effectiveAlgorithm === "centered-master" ? i18n("Center Ratio") : i18n("Master Ratio")
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
                            if (algoRoot.constants.effectiveAlgorithm === "centered-master")
                                algoRoot.constants.writeSetting("SplitRatio", value, function(v) {
                                algoRoot.kcm.autotileCenteredMasterSplitRatio = v;
                            });
                            else
                                algoRoot.constants.writeSetting("SplitRatio", value, function(v) {
                                algoRoot.kcm.autotileSplitRatio = v;
                            });
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: algoRoot.constants.effectiveAlgorithm === "three-column" || algoRoot.constants.effectiveAlgorithm === "centered-master" ? i18n("Proportion of screen width for the center column") : i18n("Proportion of screen width for the master area")

                        Binding on value {
                            value: algoRoot.constants.effectiveAlgorithm === "centered-master" ? algoRoot.constants.settingValue("SplitRatio", algoRoot.kcm.autotileCenteredMasterSplitRatio) : algoRoot.constants.settingValue("SplitRatio", algoRoot.kcm.autotileSplitRatio)
                            when: !splitRatioSlider.pressed
                            restoreMode: Binding.RestoreNone
                        }

                    }

                    Label {
                        text: Math.round(splitRatioSlider.value * 100) + "%"
                        Layout.preferredWidth: algoRoot.constants.sliderValueLabelWidth
                        horizontalAlignment: Text.AlignRight
                        font: Kirigami.Theme.fixedWidthFont
                    }

                }

                // Master count - for master-stack and centered-master
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: Kirigami.Units.smallSpacing
                    visible: algoRoot.constants.effectiveAlgorithm === "master-stack" || algoRoot.constants.effectiveAlgorithm === "centered-master"

                    Label {
                        text: algoRoot.constants.effectiveAlgorithm === "centered-master" ? i18n("Center count:") : i18n("Master count:")
                    }

                    SpinBox {
                        id: masterCountSpinBox

                        from: 1
                        to: 5
                        onValueModified: {
                            if (algoRoot.constants.effectiveAlgorithm === "centered-master")
                                algoRoot.constants.writeSetting("MasterCount", value, function(v) {
                                algoRoot.kcm.autotileCenteredMasterMasterCount = v;
                            });
                            else
                                algoRoot.constants.writeSetting("MasterCount", value, function(v) {
                                algoRoot.kcm.autotileMasterCount = v;
                            });
                        }
                        Accessible.name: algoRoot.constants.effectiveAlgorithm === "centered-master" ? i18n("Center count") : i18n("Master count")
                        ToolTip.visible: hovered
                        ToolTip.text: algoRoot.constants.effectiveAlgorithm === "centered-master" ? i18n("Number of windows in the center area") : i18n("Number of windows in the master area")

                        Binding on value {
                            value: algoRoot.constants.effectiveAlgorithm === "centered-master" ? algoRoot.constants.settingValue("MasterCount", algoRoot.kcm.autotileCenteredMasterMasterCount) : algoRoot.constants.settingValue("MasterCount", algoRoot.kcm.autotileMasterCount)
                            when: !masterCountSpinBox.activeFocus
                            restoreMode: Binding.RestoreNone
                        }

                    }

                }

            }

        }

    }

}
