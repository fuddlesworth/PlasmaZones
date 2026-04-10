// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Popup dialog for full curve editing (drag-handle bezier or spring sliders).
 */
Kirigami.Dialog {
    id: root

    property string eventLabel: ""
    property int timingMode: 0
    property string easingCurve: "0.33,1.00,0.68,1.00"
    property real springDamping: 1
    property real springStiffness: 800
    property real springEpsilon: 0.0001
    property string _workingCurve: easingCurve
    property real _workingDamping: springDamping
    property real _workingStiffness: springStiffness
    property real _workingEpsilon: springEpsilon
    property bool _savingPreset: false

    signal curveApplied(string curve)
    signal springApplied(real damping, real stiffness, real epsilon)

    title: i18n("Customize Curve — %1", eventLabel)
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.85 : Kirigami.Units.gridUnit * 40)
    preferredHeight: Math.min(Kirigami.Units.gridUnit * 32, parent ? parent.height * 0.8 : Kirigami.Units.gridUnit * 32)
    standardButtons: Kirigami.Dialog.Apply | Kirigami.Dialog.Cancel
    onApplied: {
        if (timingMode === CurvePresets.timingModeEasing)
            root.curveApplied(root._workingCurve);
        else
            root.springApplied(_workingDamping, _workingStiffness, _workingEpsilon);
        close();
    }
    onRejected: close()
    onOpened: {
        root._savingPreset = false;
        _workingCurve = easingCurve;
        _workingDamping = springDamping;
        _workingStiffness = springStiffness;
        _workingEpsilon = springEpsilon;
        if (timingMode === CurvePresets.timingModeEasing) {
            easingPreviewInDialog.curve = easingCurve;
            easingPreviewInDialog.replay();
        }
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // ── Easing mode ─────────────────────────────────────────────
        EasingPreview {
            id: easingPreviewInDialog

            visible: root.timingMode === CurvePresets.timingModeEasing
            Layout.fillWidth: true
            curve: root._workingCurve
            animationDuration: appSettings.animationDuration
            previewEnabled: root.visible && root.timingMode === CurvePresets.timingModeEasing
            onCurveEdited: function(newCurve) {
                root._workingCurve = newCurve;
            }
        }

        // ── Easing controls ─────────────────────────────────────────
        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Preset")

            WideComboBox {
                id: dialogCurvePreset

                Accessible.name: i18n("Easing preset")
                displayText: currentIndex < 0 ? i18n("Custom") : currentText
                // Full style list from shared presets
                model: CurvePresets.easingStyles.map((s) => {
                    return s.label;
                })
                currentIndex: CurvePresets.findIndices(root._workingCurve).styleIndex
                onActivated: (index) => {
                    var dirIdx = dialogDirection.currentIndex >= 0 ? dialogDirection.currentIndex : 1;
                    var curve = CurvePresets.curveForSelection(index, dirIdx);
                    if (curve) {
                        root._workingCurve = curve;
                        easingPreviewInDialog.curve = curve;
                    }
                }
            }

        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing
            title: i18n("Direction")
            description: i18n("Ease In accelerates, Ease Out decelerates")

            WideComboBox {
                id: dialogDirection

                Accessible.name: i18n("Easing direction")
                model: CurvePresets.easingDirections.map((d) => {
                    return d.label;
                })
                currentIndex: CurvePresets.findIndices(root._workingCurve).dirIndex
                onActivated: (index) => {
                    var styleIdx = dialogCurvePreset.currentIndex;
                    if (styleIdx >= 0) {
                        var curve = CurvePresets.curveForSelection(styleIdx, index);
                        if (curve) {
                            root._workingCurve = curve;
                            easingPreviewInDialog.curve = curve;
                        }
                    }
                }
            }

        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeEasing && easingPreviewInDialog.curveType !== "bezier"
        }

        // Amplitude (elastic + bounce)
        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing && (easingPreviewInDialog.curveType.indexOf("elastic") >= 0 || easingPreviewInDialog.curveType.indexOf("bounce") >= 0)
            title: i18n("Amplitude")
            description: easingPreviewInDialog.curveType.indexOf("elastic") >= 0 ? i18n("Strength of elastic overshoot") : i18n("Height of bounce peaks")

            SettingsSlider {
                Accessible.name: i18n("Amplitude")
                from: 0.5
                to: 3
                stepSize: 0.1
                value: easingPreviewInDialog.curveAmplitude
                formatValue: function(v) {
                    return v.toFixed(1);
                }
                onMoved: (value) => {
                    var ct = easingPreviewInDialog.curveType;
                    var amp = value.toFixed(2);
                    if (ct.indexOf("elastic") >= 0) {
                        var per = easingPreviewInDialog.elasticPeriod.toFixed(2);
                        var newCurve = ct + ":" + amp + "," + per;
                        root._workingCurve = newCurve;
                        easingPreviewInDialog.curve = newCurve;
                    } else {
                        var newCurve2 = ct + ":" + amp + "," + easingPreviewInDialog.bouncesCount;
                        root._workingCurve = newCurve2;
                        easingPreviewInDialog.curve = newCurve2;
                    }
                }
            }

        }

        // Period (elastic only)
        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing && easingPreviewInDialog.curveType.indexOf("elastic") >= 0
            title: i18n("Period")
            description: i18n("Oscillation speed — lower is faster wobble")

            SettingsSlider {
                Accessible.name: i18n("Period")
                from: 0.1
                to: 1
                stepSize: 0.05
                value: easingPreviewInDialog.elasticPeriod
                formatValue: function(v) {
                    return v.toFixed(2);
                }
                onMoved: (value) => {
                    var ct = easingPreviewInDialog.curveType;
                    var amp = easingPreviewInDialog.curveAmplitude.toFixed(2);
                    var newCurve = ct + ":" + amp + "," + value.toFixed(2);
                    root._workingCurve = newCurve;
                    easingPreviewInDialog.curve = newCurve;
                }
            }

        }

        // Bounces (bounce only)
        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeEasing && easingPreviewInDialog.curveType.indexOf("bounce") >= 0
            title: i18n("Bounces")
            description: i18n("Number of bounce repetitions")

            SettingsSlider {
                Accessible.name: i18n("Bounces")
                from: 1
                to: 8
                stepSize: 1
                value: easingPreviewInDialog.bouncesCount
                valueSuffix: ""
                onMoved: (value) => {
                    var ct = easingPreviewInDialog.curveType;
                    var amp = easingPreviewInDialog.curveAmplitude.toFixed(2);
                    var newCurve = ct + ":" + amp + "," + Math.round(value);
                    root._workingCurve = newCurve;
                    easingPreviewInDialog.curve = newCurve;
                }
            }

        }

        // ── Spring mode ─────────────────────────────────────────────
        SpringPreview {
            visible: root.timingMode === CurvePresets.timingModeSpring
            Layout.fillWidth: true
            dampingRatio: root._workingDamping
            stiffness: root._workingStiffness
            epsilon: root._workingEpsilon
            previewEnabled: root.visible && root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Preset")
            description: i18n("Quick-select spring behavior")

            WideComboBox {
                id: springPresetCombo

                Accessible.name: i18n("Spring preset")
                displayText: currentIndex < 0 ? i18n("Custom") : currentText
                model: CurvePresets.springPresets.map((p) => {
                    return p.label;
                })
                currentIndex: CurvePresets.springPresetIndex(root._workingDamping, root._workingStiffness, root._workingEpsilon)
                onActivated: (index) => {
                    if (index >= 0 && index < CurvePresets.springPresets.length) {
                        var p = CurvePresets.springPresets[index];
                        root._workingDamping = p.dampingRatio;
                        root._workingStiffness = p.stiffness;
                        root._workingEpsilon = p.epsilon;
                    }
                }
            }

        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Damping ratio")
            description: i18n("< 1 bouncy, = 1 critical, > 1 overdamped")

            SettingsSlider {
                Accessible.name: i18n("Damping ratio")
                from: settingsController.springDampingRatioMin
                to: settingsController.springDampingRatioMax
                stepSize: 0.05
                value: root._workingDamping
                formatValue: function(v) {
                    return v.toFixed(2);
                }
                onMoved: (value) => {
                    root._workingDamping = value;
                }
            }

        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Stiffness")
            description: i18n("Higher = faster spring response")

            SettingsSlider {
                Accessible.name: i18n("Stiffness")
                from: settingsController.springStiffnessMin
                to: settingsController.springStiffnessMax
                stepSize: 10
                valueSuffix: ""
                value: root._workingStiffness
                onMoved: (value) => {
                    root._workingStiffness = Math.round(value);
                }
            }

        }

        SettingsSeparator {
            visible: root.timingMode === CurvePresets.timingModeSpring
        }

        SettingsRow {
            visible: root.timingMode === CurvePresets.timingModeSpring
            title: i18n("Epsilon")
            description: i18n("Convergence threshold — lower means longer animation")

            SettingsSlider {
                Accessible.name: i18n("Epsilon")
                from: 0.0001
                to: 0.1
                stepSize: 0.0001
                value: root._workingEpsilon
                formatValue: function(v) {
                    return v.toFixed(4);
                }
                onMoved: (value) => {
                    root._workingEpsilon = value;
                }
            }

        }

    }

    footerLeadingComponent: Component {
        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            ToolButton {
                text: i18n("Save as Preset…")
                icon.name: "bookmark-new"
                Accessible.name: i18n("Save current curve as preset")
                visible: !root._savingPreset
                onClicked: root._savingPreset = true
            }

            TextField {
                id: saveNameField

                visible: root._savingPreset
                Layout.preferredWidth: Kirigami.Units.gridUnit * 12
                Accessible.name: i18n("Preset name")
                placeholderText: i18n("Preset name…")
                onVisibleChanged: {
                    if (visible) {
                        text = "";
                        forceActiveFocus();
                    }
                }
                onAccepted: {
                    if (text.trim().length > 0) {
                        var preset = {
                        };
                        if (root.timingMode === CurvePresets.timingModeEasing) {
                            preset.type = "easing";
                            preset.curve = root._workingCurve;
                        } else {
                            preset.type = "spring";
                            preset.dampingRatio = root._workingDamping;
                            preset.stiffness = root._workingStiffness;
                            preset.epsilon = root._workingEpsilon;
                        }
                        settingsController.addUserPreset(text.trim(), preset);
                        root._savingPreset = false;
                    }
                }
                Keys.onEscapePressed: root._savingPreset = false
            }

            ToolButton {
                visible: root._savingPreset
                icon.name: "document-save"
                Accessible.name: i18n("Save preset")
                enabled: saveNameField.text.trim().length > 0
                onClicked: saveNameField.accepted()
                ToolTip.text: i18n("Save")
                ToolTip.visible: hovered
            }

            ToolButton {
                visible: root._savingPreset
                icon.name: "dialog-cancel"
                Accessible.name: i18n("Cancel saving preset")
                onClicked: root._savingPreset = false
                ToolTip.text: i18n("Cancel")
                ToolTip.visible: hovered
            }

        }

    }

}
