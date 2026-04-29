// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

SettingsCard {
    id: root

    required property string eventPath
    required property string eventLabel
    property string eventDescription: ""
    property bool isParentNode: false

    readonly property var animBridge: settingsController.animationPage
    readonly property bool hasOverride: {
        var tree = appSettings.shaderProfileTree;
        return root.animBridge.effectForPath(root.eventPath).length > 0;
    }

    headerText: root.eventLabel
    collapsible: true

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        Label {
            visible: root.eventDescription.length > 0
            Layout.fillWidth: true
            text: root.eventDescription
            wrapMode: Text.WordWrap
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
        }

        Label {
            Layout.fillWidth: true
            text: root.animBridge.parentChainForEvent(root.eventPath)
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.disabledTextColor
            visible: text.length > 0
        }

        SettingsSeparator {}

        SettingsRow {
            title: i18n("Transition Effect")
            description: i18n("Shader effect applied during this transition")

            RowLayout {
                spacing: Kirigami.Units.smallSpacing

                ComboBox {
                    id: effectCombo

                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                    Accessible.name: i18n("Transition effect for %1", root.eventLabel)

                    model: {
                        var items = [
                            {
                                text: i18n("None (inherit)"),
                                value: ""
                            }
                        ];
                        var effects = root.animBridge.availableTransitionEffects;
                        for (var i = 0; i < effects.length; i++)
                            items.push({
                                text: effects[i].name,
                                value: effects[i].id
                            });
                        return items;
                    }

                    textRole: "text"
                    valueRole: "value"

                    Component.onCompleted: refreshSelection()

                    function refreshSelection() {
                        var current = root.animBridge.effectForPath(root.eventPath);
                        for (var i = 0; i < model.length; i++) {
                            if (model[i].value === current) {
                                currentIndex = i;
                                return;
                            }
                        }
                        currentIndex = 0;
                    }

                    onActivated: {
                        var selected = model[currentIndex].value;
                        if (selected === "")
                            root.animBridge.clearEffectForPath(root.eventPath);
                        else
                            root.animBridge.setEffectForPath(root.eventPath, selected);
                    }
                }

                Label {
                    text: root.animBridge.inheritSummaryForEvent(root.eventPath)
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                }
            }
        }

        AnimationShaderParamEditor {
            Layout.fillWidth: true
            eventPath: root.eventPath
            effectId: root.animBridge.effectForPath(root.eventPath)
        }

        Connections {
            target: appSettings
            function onShaderProfileTreeChanged() {
                effectCombo.refreshSelection();
            }
        }
    }
}
