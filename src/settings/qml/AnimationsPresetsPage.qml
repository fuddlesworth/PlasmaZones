// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Refresh user presets list
    // QVariantList from C++ Q_INVOKABLE — no typed QML equivalent
    property var userPresetsList: settingsController.userPresets()
    property bool _deletingPreset: false

    function filterPresetsWithIndex(type) {
        let result = [];
        for (let i = 0; i < root.userPresetsList.length; i++) {
            if (root.userPresetsList[i].type === type)
                result.push({
                "data": root.userPresetsList[i],
                "originalIndex": i
            });

        }
        return result;
    }

    function applyEasingAsDefault(curve: string) {
        appSettings.animationEasingCurve = curve;
        var raw = settingsController.rawProfileForEvent("global");
        raw.timingMode = CurvePresets.timingModeEasing;
        raw.easingCurve = curve;
        settingsController.setEventProfile("global", raw);
    }

    function applySpringAsDefault(damping: real, stiffness: real, epsilon: real) {
        var raw = settingsController.rawProfileForEvent("global");
        raw.timingMode = CurvePresets.timingModeSpring;
        raw.spring = {
            "dampingRatio": damping,
            "stiffness": stiffness,
            "epsilon": epsilon
        };
        settingsController.setEventProfile("global", raw);
    }

    function hasPresetsOfType(type) {
        for (var i = 0; i < root.userPresetsList.length; i++) {
            if (root.userPresetsList[i].type === type)
                return true;

        }
        return false;
    }

    contentHeight: content.implicitHeight
    clip: true

    Connections {
        function onUserAnimationPresetsChanged() {
            root.userPresetsList = settingsController.userPresets();
            root._deletingPreset = false;
        }

        target: appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // =================================================================
        // EASING PRESETS
        // =================================================================
        SettingsCard {
            headerText: i18n("Easing Presets")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Built-in easing presets
                Repeater {
                    model: CurvePresets.quickPresets

                    RowLayout {
                        required property var modelData
                        required property int index

                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            curve: modelData.curve
                            timingMode: CurvePresets.timingModeEasing
                            Accessible.name: i18n("Curve preview for %1", modelData.label)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: modelData.curve
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.label)
                            text: i18n("Use as Default")
                            onClicked: root.applyEasingAsDefault(modelData.curve)
                        }

                    }

                }

                // User easing presets
                Repeater {
                    model: root.filterPresetsWithIndex("easing")

                    RowLayout {
                        required property var modelData
                        required property int index

                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            curve: modelData.data.curve || "0.33,1.00,0.68,1.00"
                            timingMode: CurvePresets.timingModeEasing
                            Accessible.name: i18n("Curve preview for %1", modelData.data.name || i18n("Unnamed"))
                        }

                        Label {
                            Layout.fillWidth: true
                            text: i18n("★ %1", modelData.data.name || i18n("Unnamed"))
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: modelData.data.curve || ""
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.data.name || i18n("Unnamed"))
                            text: i18n("Use as Default")
                            onClicked: root.applyEasingAsDefault(modelData.data.curve)
                        }

                        Button {
                            Accessible.name: i18n("Delete preset %1", modelData.data.name || i18n("Unnamed"))
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete preset")
                            ToolTip.visible: hovered
                            enabled: !root._deletingPreset
                            onClicked: {
                                root._deletingPreset = true;
                                settingsController.removeUserPreset(modelData.originalIndex);
                            }
                        }

                    }

                }

                // Empty state
                Label {
                    visible: !root.hasPresetsOfType("easing")
                    text: i18n("No custom easing presets saved yet. Use \"Save as Preset...\" in the curve editor to create one.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    font.italic: true
                }

            }

        }

        // =================================================================
        // SPRING PRESETS
        // =================================================================
        SettingsCard {
            headerText: i18n("Spring Presets")
            collapsible: true

            contentItem: ColumnLayout {
                spacing: Kirigami.Units.smallSpacing

                // Built-in spring presets
                Repeater {
                    model: CurvePresets.springPresets

                    RowLayout {
                        required property var modelData
                        required property int index

                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            timingMode: CurvePresets.timingModeSpring
                            springDamping: modelData.dampingRatio
                            springStiffness: modelData.stiffness
                            Accessible.name: i18n("Spring preview for %1", modelData.label)
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: i18n("damping: %1, stiffness: %2", modelData.dampingRatio.toFixed(1), Math.round(modelData.stiffness))
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.label)
                            text: i18n("Use as Default")
                            onClicked: root.applySpringAsDefault(modelData.dampingRatio, modelData.stiffness, modelData.epsilon)
                        }

                    }

                }

                SettingsSeparator {
                    visible: root.hasPresetsOfType("spring")
                }

                // User spring presets
                Repeater {
                    model: root.filterPresetsWithIndex("spring")

                    RowLayout {
                        required property var modelData
                        required property int index

                        spacing: Kirigami.Units.smallSpacing

                        CurveThumbnail {
                            implicitWidth: Kirigami.Units.gridUnit * 5
                            implicitHeight: Kirigami.Units.gridUnit * 3
                            timingMode: CurvePresets.timingModeSpring
                            springDamping: modelData.data.dampingRatio ?? 1
                            springStiffness: modelData.data.stiffness ?? 800
                            Accessible.name: i18n("Spring preview for %1", modelData.data.name || i18n("Unnamed"))
                        }

                        Label {
                            Layout.fillWidth: true
                            text: i18n("★ %1", modelData.data.name || i18n("Unnamed"))
                            color: Kirigami.Theme.textColor
                        }

                        Label {
                            text: i18n("damping: %1, stiffness: %2", (modelData.data.dampingRatio ?? 1).toFixed(1), Math.round(modelData.data.stiffness ?? 800))
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                        }

                        Button {
                            Accessible.name: i18n("Use %1 as default", modelData.data.name || i18n("Unnamed"))
                            text: i18n("Use as Default")
                            onClicked: root.applySpringAsDefault(modelData.data.dampingRatio ?? 1, modelData.data.stiffness ?? 800, modelData.data.epsilon ?? 0.0001)
                        }

                        Button {
                            Accessible.name: i18n("Delete preset %1", modelData.data.name || i18n("Unnamed"))
                            icon.name: "edit-delete"
                            display: AbstractButton.IconOnly
                            ToolTip.text: i18n("Delete preset")
                            ToolTip.visible: hovered
                            enabled: !root._deletingPreset
                            onClicked: {
                                root._deletingPreset = true;
                                settingsController.removeUserPreset(modelData.originalIndex);
                            }
                        }

                    }

                }

                // Empty state
                Label {
                    visible: !root.hasPresetsOfType("spring")
                    text: i18n("No custom spring presets saved yet. Use \"Save as Preset...\" in the curve editor to create one.")
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                    font.italic: true
                }

            }

        }

    }

}
