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
        var info = currentShaderInfo;
        if (!info || !info.parameters) {
            pendingParams = {};
            return;
        }
        var defaults = {};
        for (var i = 0; i < info.parameters.length; i++) {
            var param = info.parameters[i];
            if (param && param.id !== undefined && param.default !== undefined) {
                defaults[param.id] = param.default;
            }
        }
        pendingParams = defaults;
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
        var defaults = {};
        for (var i = 0; i < shaderParams.length; i++) {
            var param = shaderParams[i];
            if (param && param.id !== undefined && param.default !== undefined) {
                defaults[param.id] = param.default;
            }
        }
        pendingParams = defaults;
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

    footer: DialogButtonBox {
        Button {
            text: i18nc("@action:button", "Defaults")
            icon.name: "edit-undo"
            visible: root.shaderParams.length > 0
            DialogButtonBox.buttonRole: DialogButtonBox.ResetRole
            onClicked: root.resetToDefaults()
        }

        Button {
            text: i18nc("@action:button", "Apply")
            icon.name: "dialog-ok-apply"
            DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            onClicked: {
                root.applyChanges();
                root.close();
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
            Layout.preferredHeight: Kirigami.Units.gridUnit * 4
            Layout.minimumHeight: Kirigami.Units.gridUnit * 4
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            visible: root.hasShaderEffect
            spacing: Kirigami.Units.smallSpacing

            // Description text - takes remaining vertical space
            Label {
                id: descriptionLabel
                Layout.fillWidth: true
                Layout.fillHeight: true

                text: {
                    if (!root.currentShaderInfo) return i18nc("@info:placeholder", "No description available");
                    var desc = root.currentShaderInfo.description;
                    return desc ? desc : i18nc("@info:placeholder", "No description available");
                }
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                maximumLineCount: 3
                opacity: (root.currentShaderInfo && root.currentShaderInfo.description) ? 0.8 : 0.5
                font: Kirigami.Theme.smallFont
                font.italic: !(root.currentShaderInfo && root.currentShaderInfo.description)
                verticalAlignment: Text.AlignTop
            }

            // Author/version metadata - natural height at bottom
            Label {
                id: metadataLabel
                Layout.fillWidth: true

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
                font: Kirigami.Theme.smallFont
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
                        Kirigami.FormLayout {
                            Repeater {
                                model: paramSection.groupParams

                                delegate: Loader {
                                    Layout.fillWidth: true
                                    visible: sourceComponent !== null

                                    required property var modelData
                                    required property int index

                                    Kirigami.FormData.label: modelData.name || modelData.id

                                    sourceComponent: {
                                        switch (modelData.type) {
                                            case "float": return floatParamComponent;
                                            case "color": return colorParamComponent;
                                            case "bool": return boolParamComponent;
                                            case "int": return intParamComponent;
                                            default: return null;
                                        }
                                    }

                                    onLoaded: {
                                        if (item) item.paramData = modelData;
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
        // ═══════════════════════════════════════════════════════════════════
        Kirigami.FormLayout {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.parameterGroups.length === 0 && root.shaderParams.length > 0

            Repeater {
                model: root.parameterGroups.length === 0 ? root.shaderParams : []

                delegate: Loader {
                    Layout.fillWidth: true
                    visible: sourceComponent !== null

                    required property var modelData
                    required property int index

                    Kirigami.FormData.label: modelData.name || modelData.id

                    sourceComponent: {
                        switch (modelData.type) {
                            case "float": return floatParamComponent;
                            case "color": return colorParamComponent;
                            case "bool": return boolParamComponent;
                            case "int": return intParamComponent;
                            default: return null;
                        }
                    }

                    onLoaded: {
                        if (item) item.paramData = modelData;
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
    // PARAMETER COMPONENTS
    // ═══════════════════════════════════════════════════════════════════════

    Component {
        id: floatParamComponent

        RowLayout {
            property var paramData

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Slider {
                id: floatSlider
                Layout.fillWidth: true

                from: paramData && paramData.min !== undefined ? paramData.min : 0
                to: paramData && paramData.max !== undefined ? paramData.max : 1
                stepSize: paramData && paramData.step !== undefined ? paramData.step : 0.01

                value: paramData ? root.parameterValue(
                    paramData.id,
                    paramData.default !== undefined ? paramData.default : 0.5
                ) : 0.5

                ToolTip.text: paramData ? (paramData.description || "") : ""
                ToolTip.visible: hovered && paramData && paramData.description
                ToolTip.delay: Kirigami.Units.toolTipDelay

                onMoved: {
                    if (paramData) {
                        root.setPendingParam(paramData.id, value);
                    }
                }
            }

            Label {
                text: floatSlider.value.toFixed(2)
                Layout.preferredWidth: root.sliderValueLabelWidth
                horizontalAlignment: Text.AlignRight
                font: Kirigami.Theme.fixedWidthFont
            }
        }
    }

    Component {
        id: colorParamComponent

        RowLayout {
            property var paramData

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Rectangle {
                id: colorSwatch

                property color currentColor: {
                    if (!paramData) return "#ffffff";
                    var colorStr = root.parameterValue(
                        paramData.id,
                        paramData.default || "#ffffff"
                    );
                    return Qt.color(colorStr);
                }

                width: root.colorButtonSize
                height: root.colorButtonSize
                radius: Kirigami.Units.smallSpacing
                color: currentColor
                border.width: 1
                border.color: Kirigami.Theme.separatorColor

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (!paramData) return;
                        shaderColorDialog.selectedColor = colorSwatch.currentColor;
                        shaderColorDialog.paramId = paramData.id;
                        shaderColorDialog.paramName = paramData.name || paramData.id;
                        shaderColorDialog.open();
                    }
                }
            }

            Label {
                text: colorSwatch.currentColor.toString().toUpperCase()
                Layout.preferredWidth: root.colorLabelWidth
                font: Kirigami.Theme.fixedWidthFont
                opacity: 0.7
            }

            Item { Layout.fillWidth: true }
        }
    }

    Component {
        id: boolParamComponent

        RowLayout {
            property var paramData

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            CheckBox {
                text: paramData ? (paramData.description || "") : ""
                checked: paramData ? root.parameterValue(
                    paramData.id,
                    paramData.default !== undefined ? paramData.default : false
                ) : false

                onToggled: {
                    if (paramData) {
                        root.setPendingParam(paramData.id, checked);
                    }
                }
            }

            Item { Layout.fillWidth: true }
        }
    }

    Component {
        id: intParamComponent

        RowLayout {
            property var paramData

            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            SpinBox {
                from: paramData && paramData.min !== undefined ? paramData.min : 0
                to: paramData && paramData.max !== undefined ? paramData.max : 100

                value: paramData ? root.parameterValue(
                    paramData.id,
                    paramData.default !== undefined ? paramData.default : 0
                ) : 0

                ToolTip.text: paramData ? (paramData.description || "") : ""
                ToolTip.visible: hovered && paramData && paramData.description
                ToolTip.delay: Kirigami.Units.toolTipDelay

                onValueModified: {
                    if (paramData) {
                        root.setPendingParam(paramData.id, value);
                    }
                }
            }

            Item { Layout.fillWidth: true }
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
