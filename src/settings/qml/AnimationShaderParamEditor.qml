// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: root

    required property string eventPath
    required property string effectId

    readonly property var animBridge: settingsController.animationPage

    spacing: Kirigami.Units.smallSpacing
    visible: root.effectId.length > 0

    property var _params: []
    property var _defaults: ({})
    property var _pending: ({})

    function loadFromBackend() {
        if (!root.effectId || root.effectId.length === 0) {
            root._params = [];
            root._defaults = {};
            root._pending = {};
            return;
        }
        root._params = root.animBridge.shaderParameters(root.effectId);
        root._defaults = root.animBridge.shaderDefaults(root.effectId);
        var stored = root.animBridge.parametersForPath(root.eventPath);
        var merged = Object.assign({}, root._defaults, stored);
        root._pending = merged;
    }

    function commitParam(paramId, value) {
        root.animBridge.setParameterForPath(root.eventPath, paramId, value);
        root._pending[paramId] = value;
    }

    function resetToDefaults() {
        for (var i = 0; i < root._params.length; i++) {
            var p = root._params[i];
            if (root._defaults[p.id] !== undefined)
                root.animBridge.setParameterForPath(root.eventPath, p.id, root._defaults[p.id]);
        }
        loadFromBackend();
    }

    Component.onCompleted: loadFromBackend()
    onEffectIdChanged: loadFromBackend()
    onEventPathChanged: loadFromBackend()

    Label {
        visible: root._params.length > 0
        text: i18n("Shader Parameters")
        font.bold: true
        Layout.fillWidth: true
        Layout.topMargin: Kirigami.Units.smallSpacing
    }

    Repeater {
        model: root._params

        delegate: RowLayout {
            id: paramRow

            required property var modelData
            required property int index

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: paramRow.modelData.name || paramRow.modelData.id || ""
                Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                elide: Text.ElideRight
            }

            Loader {
                Layout.fillWidth: true
                sourceComponent: {
                    var t = paramRow.modelData.type || "float";
                    if (t === "float")
                        return floatSliderComponent;
                    if (t === "int")
                        return intSliderComponent;
                    if (t === "bool")
                        return boolCheckComponent;
                    return floatSliderComponent;
                }

                property var paramData: paramRow.modelData
                property var currentValue: root._pending[paramRow.modelData.id] !== undefined ? root._pending[paramRow.modelData.id] : (paramRow.modelData["default"] !== undefined ? paramRow.modelData["default"] : 0)
            }
        }
    }

    Button {
        visible: root._params.length > 0
        text: i18n("Reset to Defaults")
        icon.name: "edit-undo"
        Layout.alignment: Qt.AlignRight
        onClicked: root.resetToDefaults()
    }

    Component {
        id: floatSliderComponent

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Slider {
                Layout.fillWidth: true
                from: parent.parent.paramData ? (parent.parent.paramData.min !== undefined ? parent.parent.paramData.min : 0.0) : 0.0
                to: parent.parent.paramData ? (parent.parent.paramData.max !== undefined ? parent.parent.paramData.max : 1.0) : 1.0
                value: parent.parent.currentValue || 0
                onMoved: root.commitParam(parent.parent.paramData.id, value)
            }

            Label {
                text: (parent.parent.currentValue || 0).toFixed(2)
                Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                horizontalAlignment: Text.AlignRight
                font: Kirigami.Theme.smallFont
            }
        }
    }

    Component {
        id: intSliderComponent

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Slider {
                Layout.fillWidth: true
                from: parent.parent.paramData ? (parent.parent.paramData.min !== undefined ? parent.parent.paramData.min : 0) : 0
                to: parent.parent.paramData ? (parent.parent.paramData.max !== undefined ? parent.parent.paramData.max : 10) : 10
                stepSize: 1
                value: parent.parent.currentValue || 0
                onMoved: root.commitParam(parent.parent.paramData.id, Math.round(value))
            }

            Label {
                text: Math.round(parent.parent.currentValue || 0)
                Layout.preferredWidth: Kirigami.Units.gridUnit * 3
                horizontalAlignment: Text.AlignRight
                font: Kirigami.Theme.smallFont
            }
        }
    }

    Component {
        id: boolCheckComponent

        CheckBox {
            checked: parent.parent.currentValue || false
            onToggled: root.commitParam(parent.parent.paramData.id, checked)
        }
    }
}
