// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Shader settings dialog
 *
 * Provides a consistent UX for configuring shader effects on zone overlays.
 * Uses section-based layout appropriate for dialogs.
 * Supports both grouped parameters (with accordion sections) and flat parameters.
 */
Kirigami.Dialog {
    id: root

    required property var editorController

    title: i18nc("@title:window", "Shader Settings")
    preferredWidth: Kirigami.Units.gridUnit * 28
    preferredHeight: Kirigami.Units.gridUnit * 30
    padding: Kirigami.Units.largeSpacing

    // ═══════════════════════════════════════════════════════════════════════
    // CONSTANTS
    // ═══════════════════════════════════════════════════════════════════════
    readonly property int colorButtonSize: Kirigami.Units.gridUnit * 3

    // Theme colors cached at dialog level for use in dynamically loaded Components
    // Use disabledTextColor with reduced opacity as separator color
    readonly property color themeSeparatorColor: Qt.rgba(
        Kirigami.Theme.textColor.r,
        Kirigami.Theme.textColor.g,
        Kirigami.Theme.textColor.b,
        0.2
    )
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 4
    readonly property int colorLabelWidth: Kirigami.Units.gridUnit * 7

    // ═══════════════════════════════════════════════════════════════════════
    // PENDING STATE (buffered until Apply is clicked)
    // ═══════════════════════════════════════════════════════════════════════
    property var pendingParams: ({})
    property string pendingShaderId: ""

    // Accordion state - only one group expanded at a time (-1 = all collapsed)
    property int expandedGroupIndex: 0

    // ═══════════════════════════════════════════════════════════════════════
    // COMPUTED PROPERTIES
    // ═══════════════════════════════════════════════════════════════════════
    readonly property string noneShaderId: editorController ? editorController.noneShaderUuid : ""
    readonly property bool hasShaderEffect: pendingShaderId !== "" && pendingShaderId !== noneShaderId

    readonly property var currentShaderInfo: {
        if (!editorController) return null;
        var shaders = editorController.availableShaders;
        if (!shaders || !pendingShaderId) return null;
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i] && shaders[i].id === pendingShaderId) {
                return shaders[i];
            }
        }
        return null;
    }

    readonly property var shaderParams: currentShaderInfo ? (currentShaderInfo.parameters || []) : []

    // Computed property: parameters grouped by their "group" field
    readonly property var parameterGroups: buildParameterGroups(shaderParams)

    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    onOpened: {
        initializePendingState();
        expandedGroupIndex = 0;
    }

    onPendingShaderIdChanged: {
        if (visible) {
            initializePendingParamsForShader();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STATE MANAGEMENT FUNCTIONS
    // ═══════════════════════════════════════════════════════════════════════
    function initializePendingState() {
        if (!editorController) return;
        pendingShaderId = editorController.currentShaderId || "";
        var current = editorController.currentShaderParams || {};
        var copy = {};
        for (var key in current) {
            copy[key] = current[key];
        }
        pendingParams = copy;
    }

    function initializePendingParamsForShader() {
        // Get params directly by ID to avoid stale computed property values
        // When onPendingShaderIdChanged fires, currentShaderInfo may not have updated yet
        var params = getShaderParamsById(pendingShaderId);
        if (!params || params.length === 0) {
            pendingParams = {};
            return;
        }
        pendingParams = extractDefaults(params);
    }

    // Helper to get shader parameters directly by ID, avoiding computed property timing issues
    function getShaderParamsById(shaderId) {
        if (!editorController || !shaderId) return [];
        var shaders = editorController.availableShaders;
        if (!shaders) return [];
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i] && shaders[i].id === shaderId) {
                return shaders[i].parameters || [];
            }
        }
        return [];
    }

    function setPendingParam(paramId, value) {
        var copy = {};
        for (var key in pendingParams) {
            copy[key] = pendingParams[key];
        }
        copy[paramId] = value;
        pendingParams = copy;
    }

    function parameterValue(paramId, fallback) {
        if (pendingParams && pendingParams[paramId] !== undefined) {
            return pendingParams[paramId];
        }
        return fallback;
    }

    function applyChanges() {
        if (!editorController) return;

        if (pendingShaderId !== editorController.currentShaderId) {
            editorController.currentShaderId = pendingShaderId;
        }

        var currentParams = editorController.currentShaderParams || {};
        for (var paramId in pendingParams) {
            if (pendingParams[paramId] !== currentParams[paramId]) {
                editorController.setShaderParameter(paramId, pendingParams[paramId]);
            }
        }
    }

    function resetToDefaults() {
        if (!editorController) return;
        pendingParams = extractDefaults(shaderParams);
    }

    function randomizeParameters() {
        if (!editorController || shaderParams.length === 0) return;

        var randomized = {};
        for (var i = 0; i < shaderParams.length; i++) {
            var param = shaderParams[i];
            if (!param || param.id === undefined) continue;

            var value;
            switch (param.type) {
                case "float":
                    var minF = param.min !== undefined ? param.min : 0;
                    var maxF = param.max !== undefined ? param.max : 1;
                    value = minF + Math.random() * (maxF - minF);
                    // Round to step if defined
                    if (param.step !== undefined && param.step > 0) {
                        value = Math.round(value / param.step) * param.step;
                    }
                    break;

                case "int":
                    var minI = param.min !== undefined ? param.min : 0;
                    var maxI = param.max !== undefined ? param.max : 100;
                    value = Math.floor(minI + Math.random() * (maxI - minI + 1));
                    break;

                case "bool":
                    value = Math.random() < 0.5;
                    break;

                case "color":
                    // Generate random RGB color
                    var r = Math.floor(Math.random() * 256);
                    var g = Math.floor(Math.random() * 256);
                    var b = Math.floor(Math.random() * 256);
                    value = "#" + r.toString(16).padStart(2, "0")
                                + g.toString(16).padStart(2, "0")
                                + b.toString(16).padStart(2, "0");
                    break;

                default:
                    // Unknown type - use default if available
                    value = param.default;
                    break;
            }

            if (value !== undefined) {
                randomized[param.id] = value;
            }
        }
        pendingParams = randomized;
    }

    // Extract default values from parameter definitions
    function extractDefaults(params) {
        var defaults = {};
        if (!params) return defaults;
        for (var i = 0; i < params.length; i++) {
            var param = params[i];
            if (param && param.id !== undefined && param.default !== undefined) {
                defaults[param.id] = param.default;
            }
        }
        return defaults;
    }

    // Helper for ParameterDelegate to open color dialog
    function openColorDialog(paramId, paramName, currentColor) {
        shaderColorDialog.selectedColor = currentColor;
        shaderColorDialog.paramId = paramId;
        shaderColorDialog.paramName = paramName;
        shaderColorDialog.open();
    }

    function firstEffectId() {
        var shaders = editorController ? editorController.availableShaders : [];
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i] && shaders[i].id && shaders[i].id !== noneShaderId) {
                return shaders[i].id;
            }
        }
        return noneShaderId;
    }

    function buildParameterGroups(params) {
        if (!params || params.length === 0) return [];

        var hasGroups = false;
        for (var i = 0; i < params.length; i++) {
            if (params[i] && params[i].group) {
                hasGroups = true;
                break;
            }
        }

        if (!hasGroups) return [];

        var groupMap = {};
        var groupOrder = [];
        for (var j = 0; j < params.length; j++) {
            var param = params[j];
            if (!param) continue;
            var groupName = param.group || i18nc("@title:group", "General");
            if (!groupMap[groupName]) {
                groupMap[groupName] = [];
                groupOrder.push(groupName);
            }
            groupMap[groupName].push(param);
        }

        var result = [];
        for (var k = 0; k < groupOrder.length; k++) {
            var name = groupOrder[k];
            result.push({ name: name, params: groupMap[name] });
        }
        return result;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // FOOTER BUTTONS
    // ═══════════════════════════════════════════════════════════════════════
    standardButtons: Kirigami.Dialog.NoButton

    footer: Item {
        implicitHeight: footerLayout.implicitHeight + Kirigami.Units.largeSpacing * 2
        implicitWidth: footerLayout.implicitWidth

        RowLayout {
            id: footerLayout
            anchors.fill: parent
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            // Left side - Random button
            Button {
                text: i18nc("@action:button", "Random")
                icon.name: "roll"
                visible: root.shaderParams.length > 0
                onClicked: root.randomizeParameters()
            }

            Item { Layout.fillWidth: true }

            // Right side - Defaults and Apply
            Button {
                text: i18nc("@action:button", "Defaults")
                icon.name: "edit-undo"
                visible: root.shaderParams.length > 0
                onClicked: root.resetToDefaults()
            }

            Button {
                text: i18nc("@action:button", "Apply")
                icon.name: "dialog-ok-apply"
                onClicked: {
                    root.applyChanges();
                    root.close();
                }
            }
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // MAIN CONTENT
    // ═══════════════════════════════════════════════════════════════════════
    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════
        // EFFECT SELECTION SECTION
        // ═══════════════════════════════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true

            CheckBox {
                id: enableEffectCheck
                Kirigami.FormData.label: i18nc("@label", "Enable effect:")
                text: i18nc("@option:check", "Enable shader effect")
                checked: root.hasShaderEffect

                onToggled: {
                    if (checked) {
                        if (root.pendingShaderId === root.noneShaderId) {
                            root.pendingShaderId = root.firstEffectId();
                        }
                    } else {
                        root.pendingShaderId = root.noneShaderId;
                    }
                }
            }

            ComboBox {
                id: shaderComboBox
                Kirigami.FormData.label: i18nc("@label", "Shader:")
                enabled: root.hasShaderEffect
                Layout.fillWidth: true

                model: root.editorController ? root.editorController.availableShaders : []
                textRole: "name"
                valueRole: "id"

                function findShaderIndex() {
                    if (!model) return 0;
                    for (var i = 0; i < model.length; i++) {
                        if (model[i] && model[i].id === root.pendingShaderId) return i;
                    }
                    return 0;
                }

                Component.onCompleted: currentIndex = findShaderIndex()

                onActivated: {
                    if (currentValue !== undefined) {
                        root.pendingShaderId = currentValue;
                    }
                }

                Connections {
                    target: root
                    function onPendingShaderIdChanged() {
                        shaderComboBox.currentIndex = shaderComboBox.findShaderIndex();
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // SHADER DESCRIPTION AREA (OUTSIDE FormLayout)
        // FormLayout has special internal handling that conflicts with
        // fixed-height containers. Moving this to the parent ColumnLayout
        // gives reliable sizing behavior.
        // ═══════════════════════════════════════════════════════════════════
        ColumnLayout {
            id: descriptionArea
            Layout.fillWidth: true
            Layout.preferredWidth: 0  // Don't expand parent based on content
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            Layout.minimumHeight: Kirigami.Units.gridUnit * 4
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: root.hasShaderEffect
            spacing: Kirigami.Units.smallSpacing

            // Description text - takes remaining vertical space
            // Layout.preferredWidth: 0 prevents implicitWidth from expanding layout
            Label {
                id: descriptionLabel
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 0

                text: {
                    if (!root.currentShaderInfo) return i18nc("@info:placeholder", "No description available");
                    var desc = root.currentShaderInfo.description;
                    return desc ? desc : i18nc("@info:placeholder", "No description available");
                }
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 3
                opacity: (root.currentShaderInfo && root.currentShaderInfo.description) ? 0.8 : 0.5
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                font.italic: !(root.currentShaderInfo && root.currentShaderInfo.description)
                verticalAlignment: Text.AlignTop
            }

            // Author/version metadata - natural height at bottom
            Label {
                id: metadataLabel
                Layout.fillWidth: true
                Layout.preferredWidth: 0

                text: {
                    if (!root.currentShaderInfo) return "";
                    var info = root.currentShaderInfo;
                    var parts = [];
                    if (info.author) parts.push(i18nc("@info shader author", "by %1", info.author));
                    if (info.version) parts.push(i18nc("@info shader version", "v%1", info.version));
                    if (info.isUserShader) parts.push(i18nc("@info user-installed shader", "(User shader)"));
                    return parts.join(" · ");
                }
                elide: Text.ElideRight
                opacity: 0.5
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                font.italic: true
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // PARAMETERS SECTION
        // ═══════════════════════════════════════════════════════════════════
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.shaderParams.length > 0
        }

        Label {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.shaderParams.length > 0
            text: i18nc("@title:group", "Parameters")
            font.weight: Font.DemiBold
        }

        // Message when shader has no parameters
        Label {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.shaderParams.length === 0
            text: i18nc("@info", "This effect has no configurable parameters.")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }

        // ═══════════════════════════════════════════════════════════════════
        // GROUPED PARAMETERS - Accordion sections when groups defined
        // ═══════════════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.parameterGroups.length > 0
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                id: groupRepeater
                model: root.parameterGroups

                delegate: ShaderParameterSection {
                    id: paramSection
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    title: modelData.name
                    groupParams: modelData.params

                    expanded: root.expandedGroupIndex === index

                    onToggled: {
                        root.expandedGroupIndex = expanded ? -1 : index;
                    }

                    contentItem: Component {
                        ColumnLayout {
                            Kirigami.Theme.inherit: true
                            spacing: Kirigami.Units.smallSpacing

                            Repeater {
                                model: paramSection.groupParams

                                delegate: RowLayout {
                                    required property var modelData
                                    required property int index

                                    Layout.fillWidth: true
                                    spacing: Kirigami.Units.largeSpacing

                                    Label {
                                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                                        text: modelData ? (modelData.name || modelData.id || "") : ""
                                        horizontalAlignment: Text.AlignRight
                                        elide: Text.ElideRight
                                    }

                                    ParameterDelegate {
                                        Layout.fillWidth: true
                                        Kirigami.Theme.inherit: true
                                        dialogRoot: root
                                        paramData: modelData
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // FLAT PARAMETERS - When no groups are defined
        // Uses ColumnLayout instead of FormLayout to avoid attached property
        // issues during Repeater model changes (FormLayout caches FormData refs)
        // ═══════════════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.parameterGroups.length === 0 && root.shaderParams.length > 0
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: root.parameterGroups.length === 0 ? root.shaderParams : []

                delegate: RowLayout {
                    required property var modelData
                    required property int index

                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Label {
                        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        text: modelData ? (modelData.name || modelData.id || "") : ""
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }

                    ParameterDelegate {
                        Layout.fillWidth: true
                        Kirigami.Theme.inherit: true
                        dialogRoot: root
                        paramData: modelData
                    }
                }
            }
        }

        // ═══════════════════════════════════════════════════════════════════
        // DISABLED STATE MESSAGE
        // ═══════════════════════════════════════════════════════════════════
        Label {
            Layout.fillWidth: true
            visible: !root.hasShaderEffect
            text: i18nc("@info", "Enable the shader effect to configure visual effects for zone overlays.")
            wrapMode: Text.WordWrap
            opacity: 0.7
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // COLOR DIALOG
    // ═══════════════════════════════════════════════════════════════════════
    ColorDialog {
        id: shaderColorDialog

        property string paramId: ""
        property string paramName: ""

        title: i18nc("@title:window", "Choose %1", paramName)

        onAccepted: {
            if (paramId) {
                root.setPendingParam(paramId, selectedColor.toString());
            }
        }
    }
}
