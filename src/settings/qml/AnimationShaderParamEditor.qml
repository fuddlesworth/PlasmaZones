// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common

/**
 * @brief Inline shader parameter editor for animation event cards.
 *
 * Provides the dialogRoot interface expected by ParameterDelegate,
 * backed by the animation profile tree for persistence via settingsController.
 */
Item {
    id: root

    required property string eventName
    required property string shaderId
    // ── dialogRoot interface for ParameterDelegate ──────────────
    readonly property int colorButtonSize: Kirigami.Units.gridUnit * 3
    readonly property color themeSeparatorColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 4
    readonly property int colorLabelWidth: Kirigami.Units.gridUnit * 7
    property var pendingParams: ({
    })
    property var lockedParams: ({
    })
    property bool _committing: false
    property string imageDialogParamId: ""
    // Parameter metadata from registry
    readonly property var shaderParams: root.shaderId !== "" ? settingsController.animationShaderParameters(root.shaderId) : []
    readonly property var parameterGroups: buildParameterGroups(shaderParams)
    property int expandedGroupIndex: 0
    // ── Internal ────────────────────────────────────────────────
    property string colorDialogParamId: ""

    function parameterValue(paramId, fallback) {
        if (pendingParams && pendingParams[paramId] !== undefined)
            return pendingParams[paramId];

        return fallback;
    }

    function setPendingParam(paramId, value) {
        var copy = {
        };
        for (var key in pendingParams) copy[key] = pendingParams[key]
        copy[paramId] = value;
        pendingParams = copy;
        // Params changed manually — no longer matches a preset
        if (presetCombo)
            presetCombo.currentIndex = 0;

        commitParams();
    }

    function isParamLocked(paramId) {
        return false;
    }

    function setParamLocked(paramId, locked) {
    }

    function openColorDialog(paramId, paramName, currentColor) {
        colorDialogParamId = paramId;
        colorDialog.selectedColor = currentColor;
        colorDialog.title = i18n("Choose %1", paramName);
        colorDialog.open();
    }

    function openImageDialog(paramId) {
        imageDialogParamId = paramId;
        imageDialog.open();
    }

    function mergeWithDefaults(overrides) {
        var defaults = settingsController.animationShaderDefaults(root.shaderId);
        var merged = {
        };
        for (var key in defaults) merged[key] = defaults[key]
        if (overrides) {
            for (var key2 in overrides) merged[key2] = overrides[key2]
        }
        return merged;
    }

    function loadFromTree() {
        var raw = settingsController.rawProfileForEvent(root.eventName);
        var params = raw.shaderParams || {
        };
        pendingParams = mergeWithDefaults(params);
        // Reset preset selection since params may have changed externally
        if (presetCombo)
            presetCombo.currentIndex = 0;

    }

    function commitParams() {
        // Read-modify-write: safe because QML is single-threaded.
        // Only shaderParams is modified; other profile fields are untouched.
        _committing = true;
        var raw = settingsController.rawProfileForEvent(root.eventName);
        raw.shaderParams = pendingParams;
        settingsController.setEventProfile(root.eventName, raw);
        _committing = false;
    }

    function buildParameterGroups(params) {
        if (!params || params.length === 0)
            return [];

        var hasGroups = false;
        for (var i = 0; i < params.length; i++) {
            if (params[i] && params[i].group) {
                hasGroups = true;
                break;
            }
        }
        if (!hasGroups)
            return [];

        var groupMap = {
        };
        var groupOrder = [];
        for (var j = 0; j < params.length; j++) {
            var param = params[j];
            if (!param)
                continue;

            var groupName = param.group || i18n("General");
            if (!groupMap[groupName]) {
                groupMap[groupName] = [];
                groupOrder.push(groupName);
            }
            groupMap[groupName].push(param);
        }
        var result = [];
        for (var k = 0; k < groupOrder.length; k++) {
            var name = groupOrder[k];
            result.push({
                "name": name,
                "params": groupMap[name]
            });
        }
        return result;
    }

    implicitHeight: content.implicitHeight
    Layout.fillWidth: true
    visible: root.shaderId !== "" && root.shaderParams.length > 0
    Component.onCompleted: {
        if (root.shaderId !== "")
            loadFromTree();

    }
    onShaderIdChanged: {
        if (shaderId !== "")
            Qt.callLater(loadFromTree);

    }

    Connections {
        function onAnimationProfileTreeChanged() {
            if (root.shaderId !== "" && !root._committing)
                Qt.callLater(root.loadFromTree);

        }

        target: appSettings
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.smallSpacing

        // ── Grouped parameters ──────────────────────────────
        Repeater {
            model: root.parameterGroups.length > 0 ? root.parameterGroups : null

            delegate: ShaderParameterSection {
                id: paramSection

                required property var modelData
                required property int index

                Layout.fillWidth: true
                title: modelData.name
                groupParams: modelData.params
                expanded: root.expandedGroupIndex === index
                dialogRoot: root
                onToggled: root.expandedGroupIndex = (expanded ? -1 : index)

                contentComponent: Component {
                    ColumnLayout {
                        Kirigami.Theme.inherit: true
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: paramSection.groupParams

                            delegate: RowLayout {
                                required property var modelData

                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    text: modelData.name || modelData.id || ""
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                    elide: Text.ElideRight
                                }

                                ParameterDelegate {
                                    Layout.fillWidth: true
                                    paramData: modelData
                                    dialogRoot: paramSection.dialogRoot
                                }

                            }

                        }

                    }

                }

            }

        }

        // ── Flat parameters (no groups) ─────────────────────
        // NOTE: This delegate layout mirrors the grouped version above.
        // Kept inline to avoid Loader/Component scoping issues with required properties.
        Repeater {
            model: root.parameterGroups.length === 0 ? root.shaderParams : null

            delegate: RowLayout {
                required property var modelData

                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: modelData.name || modelData.id || ""
                    Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                    elide: Text.ElideRight
                }

                ParameterDelegate {
                    Layout.fillWidth: true
                    paramData: modelData
                    dialogRoot: root
                }

            }

        }

        // ── Presets ─────────────────────────────────────────
        RowLayout {
            visible: root.shaderId !== ""
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: i18n("Preset:")
                Layout.alignment: Qt.AlignVCenter
            }

            ComboBox {
                id: presetCombo

                Layout.fillWidth: true
                Accessible.name: i18n("Shader preset")
                model: {
                    var presets = settingsController.animationShaderPresets(root.shaderId);
                    var names = [i18n("Custom")];
                    for (var i = 0; i < presets.length; i++) names.push(presets[i].name || i18n("Preset %1", i + 1))
                    return names;
                }
                onActivated: (index) => {
                    if (index === 0)
                        return ;

                    var presets = settingsController.animationShaderPresets(root.shaderId);
                    if (index - 1 < presets.length) {
                        var preset = presets[index - 1];
                        root.pendingParams = root.mergeWithDefaults(preset.params || {
                        });
                        root.commitParams();
                    }
                }
            }

            Button {
                text: i18n("Reset")
                icon.name: "edit-undo"
                Accessible.name: i18n("Reset shader parameters to defaults")
                onClicked: {
                    root.pendingParams = root.mergeWithDefaults({
                    });
                    root.commitParams();
                }
            }

        }

    }

    ColorDialog {
        id: colorDialog

        onAccepted: {
            if (root.colorDialogParamId !== "")
                root.setPendingParam(root.colorDialogParamId, selectedColor.toString());

        }
    }

    FileDialog {
        id: imageDialog

        title: i18n("Choose Image")
        nameFilters: [i18n("Image files (*.png *.jpg *.jpeg *.svg *.svgz)")]
        onAccepted: {
            if (root.imageDialogParamId) {
                var path = selectedFile.toString().replace(/^file:\/\/+/, "/");
                root.setPendingParam(root.imageDialogParamId, path);
            }
        }
    }

}
