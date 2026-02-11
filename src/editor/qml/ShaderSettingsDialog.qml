// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common 1.0
import PlasmaZones 1.0

/**
 * @brief Shader settings dialog
 *
 * Provides a consistent UX for configuring shader effects on zone overlays.
 * Side-by-side layout: settings on the left, live preview on the right.
 * Supports both grouped parameters (with accordion sections) and flat parameters.
 */
Kirigami.Dialog {
    id: root

    required property var editorController
    property var editorWindow: null

    title: i18nc("@title:window", "Shader Settings")
    preferredWidth: root.hasShaderEffect ? Kirigami.Units.gridUnit * 56 : Kirigami.Units.gridUnit * 32
    preferredHeight: root.hasShaderEffect ? Kirigami.Units.gridUnit * 32 : Kirigami.Units.gridUnit * 10
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

    // Prevent preview updates after dialog closed (avoids race with debounce timer)
    property bool previewAllowed: true

    // Preset load/save error message (shown inline when non-empty)
    property string presetErrorMessage: ""

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

    // Shaders sorted by name for the dropdown (avoids arbitrary QHash order)
    readonly property var sortedShaders: {
        var shaders = editorController ? editorController.availableShaders : [];
        if (!shaders || shaders.length === 0) return [];
        var arr = [];
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i]) arr.push(shaders[i]);
        }
        arr.sort(function(a, b) {
            var na = (a && a.name !== undefined) ? String(a.name) : "";
            var nb = (b && b.name !== undefined) ? String(b.name) : "";
            return na.localeCompare(nb);
        });
        return arr;
    }

    // Computed property: parameters grouped by their "group" field
    readonly property var parameterGroups: buildParameterGroups(shaderParams)

    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    onOpened: {
        previewAllowed = true;
        presetErrorMessage = "";
        initializePendingState();
        expandedGroupIndex = 0;
        Qt.callLater(root.updateDaemonShaderPreview);
    }

    Connections {
        target: editorController
        enabled: editorController !== null
        function onShaderPresetLoadFailed(error) {
            root.presetErrorMessage = error;
        }
        function onShaderPresetSaveFailed(error) {
            root.presetErrorMessage = error;
        }
    }

    // Hide overlay when editor loses focus (alt-tab, another app, native dialogs);
    // restore when editor becomes active again. Only applies while dialog is open.
    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive

    onAppActiveChanged: {
        if (!root.visible || !root.hasShaderEffect) return;
        if (appActive) {
            root.restoreShaderPreview();
        } else {
            root.hideShaderPreview();
        }
    }

    onClosed: {
        previewAllowed = false;
        debouncePreviewUpdate.stop();
        root.hideShaderPreview();
    }

    // Centralize overlay visibility to avoid duplication and ensure consistent behavior
    function hideShaderPreview() {
        if (editorController) {
            editorController.hideShaderPreviewOverlay();
        }
    }

    function restoreShaderPreview() {
        Qt.callLater(root.updateDaemonShaderPreview);
    }

    // Update daemon overlay when params/shader change or layout settles
    function updateDaemonShaderPreview() {
        if (!previewAllowed) {
            root.hideShaderPreview();
            return;
        }
        if (!editorController || !editorWindow || !root.hasShaderEffect
            || root.pendingShaderId === root.noneShaderId) {
            root.hideShaderPreview();
            return;
        }
        if (!previewContainer.visible || previewBackground.width <= 0 || previewBackground.height <= 0) {
            return;
        }
        var pt = previewBackground.mapToItem(editorWindow.contentItem, 0, 0);
        var gx = Math.round(editorWindow.x + pt.x);
        var gy = Math.round(editorWindow.y + pt.y);
        var w = Math.max(1, Math.floor(previewBackground.width));
        var h = Math.max(1, Math.floor(previewBackground.height));
        var zones = editorController.zonesForShaderPreview(w, h);
        var params = editorController.translateShaderParams(root.pendingShaderId, root.pendingParams || {});
        var zonesJson = JSON.stringify(zones);
        var paramsJson = JSON.stringify(params);
        var screenName = "";
        editorController.showShaderPreviewOverlay(gx, gy, w, h, screenName,
                                                 root.pendingShaderId, paramsJson, zonesJson);
    }

    onPendingShaderIdChanged: {
        if (visible) {
            initializePendingParamsForShader();
        }
        debouncePreviewUpdate.restart();
    }

    onPendingParamsChanged: debouncePreviewUpdate.restart()

    Timer {
        id: debouncePreviewUpdate
        interval: 150
        repeat: false
        onTriggered: root.updateDaemonShaderPreview()
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
            // Shader switched: atomic undo of ID + params (single Ctrl+Z)
            editorController.switchShader(pendingShaderId, pendingParams);
        } else {
            // Same shader: apply individual param changes (supports undo merge for slider drags)
            var currentParams = editorController.currentShaderParams || {};
            for (var paramId in pendingParams) {
                if (pendingParams[paramId] !== currentParams[paramId]) {
                    editorController.setShaderParameter(paramId, pendingParams[paramId]);
                }
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
        root.hideShaderPreview();
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

    function filePathFromUrl(url) {
        if (!url) return "";
        return url.toString().replace(/^file:\/\/+/, "/");
    }

    function preparePresetDialog(dialog) {
        if (editorController) {
            var dir = editorController.shaderPresetDirectory();
            if (dir && dir.length > 0) {
                dialog.currentFolder = Qt.resolvedUrl("file://" + dir);
            }
        }
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

    standardButtons: Kirigami.Dialog.NoButton

    // ═══════════════════════════════════════════════════════════════════════
    // MAIN CONTENT - Side-by-side: settings left, preview right
    // ═══════════════════════════════════════════════════════════════════════
    RowLayout {
        spacing: Kirigami.Units.largeSpacing
        Layout.fillWidth: true
        Layout.fillHeight: root.hasShaderEffect

        // ═══════════════════════════════════════════════════════════════════
        // LEFT: Settings panel - original layout/width (28gu), unchanged
        // ═══════════════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillHeight: root.hasShaderEffect
            Layout.preferredWidth: Kirigami.Units.gridUnit * 28
            Layout.minimumWidth: Kirigami.Units.gridUnit * 28
            spacing: Kirigami.Units.largeSpacing

            // ═══════════════════════════════════════════════════════════════
            // EFFECT SELECTION SECTION
            // ═══════════════════════════════════════════════════════════════
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

                model: root.sortedShaders
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
        // Shader preview is now in the editor canvas (zones rendered with shader)
        // ═══════════════════════════════════════════════════════════════════
        Kirigami.Separator {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.shaderParams.length > 0
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.hasShaderEffect && root.shaderParams.length > 0
            spacing: Kirigami.Units.smallSpacing

            Label {
                Layout.fillWidth: true
                text: i18nc("@title:group", "Parameters")
                font.weight: Font.DemiBold
            }

            ToolButton {
                icon.name: "roll"
                display: ToolButton.IconOnly
                ToolTip.text: i18nc("@info:tooltip", "Randomize all parameters")
                Accessible.name: i18nc("@action:button", "Random")
                Accessible.description: ToolTip.text
                onClicked: root.randomizeParameters()
            }
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

        // Spacer: pushes footer buttons to bottom when shader enabled; minimal gap when disabled
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: root.hasShaderEffect
            Layout.minimumHeight: root.hasShaderEffect ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.smallSpacing
        }

        // ═══════════════════════════════════════════════════════════════════
        // PRESET ERROR MESSAGE (inline when load/save fails)
        // ═══════════════════════════════════════════════════════════════════
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            visible: root.presetErrorMessage !== ""
            type: Kirigami.MessageType.Error
            text: root.presetErrorMessage
            actions: [
                Kirigami.Action {
                    icon.name: "dialog-close"
                    text: i18nc("@action:button", "Dismiss")
                    onTriggered: root.presetErrorMessage = ""
                }
            ]
        }

        // ═══════════════════════════════════════════════════════════════════
        // FOOTER BUTTONS - Right-aligned at bottom of panel
        // ═══════════════════════════════════════════════════════════════════
        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.smallSpacing

            Button {
                text: i18nc("@action:button", "Load Preset")
                icon.name: "document-open"
                ToolTip.text: i18nc("@info:tooltip", "Load shader settings from a preset file")
                Accessible.name: text
                Accessible.description: ToolTip.text
                onClicked: {
                    preparePresetDialog(loadPresetDialog);
                    root.hideShaderPreview();
                    loadPresetDialog.open();
                }
            }

            Button {
                text: i18nc("@action:button", "Save Preset")
                icon.name: "document-save-as"
                enabled: root.hasShaderEffect
                ToolTip.text: i18nc("@info:tooltip", "Save current shader settings as a preset file")
                Accessible.name: text
                Accessible.description: ToolTip.text
                onClicked: {
                    preparePresetDialog(savePresetDialog);
                    root.hideShaderPreview();
                    savePresetDialog.open();
                }
            }

            Item { Layout.fillWidth: true }

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

        // ═══════════════════════════════════════════════════════════════════
        // RIGHT: Live shader preview (hidden when effect disabled - no space)
        // ═══════════════════════════════════════════════════════════════════
        Item {
            id: previewContainer
            Layout.preferredWidth: root.hasShaderEffect ? Kirigami.Units.gridUnit * 24 : 0
            Layout.preferredHeight: root.hasShaderEffect ? Kirigami.Units.gridUnit * 22 : 0
            Layout.minimumWidth: root.hasShaderEffect ? Kirigami.Units.gridUnit * 20 : 0
            Layout.minimumHeight: root.hasShaderEffect ? Kirigami.Units.gridUnit * 18 : 0
            Layout.maximumHeight: root.hasShaderEffect ? Kirigami.Units.gridUnit * 22 : 0
            Layout.alignment: Qt.AlignCenter
            visible: root.hasShaderEffect
            clip: true

            Rectangle {
                id: previewBackground
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                color: Kirigami.Theme.backgroundColor
                radius: Kirigami.Units.smallSpacing
            }
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
            root.restoreShaderPreview();
        }
        onRejected: function() { root.restoreShaderPreview(); }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SHADER PRESET FILE DIALOGS
    // ═══════════════════════════════════════════════════════════════════════
    FileDialog {
        id: savePresetDialog

        title: i18nc("@title:window", "Save Shader Preset")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)"), i18nc("@item:inlistbox", "All files (*)")]
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"

        onAccepted: {
            if (!editorController) return;
            var path = filePathFromUrl(selectedFile);
            if (!path.toLowerCase().endsWith(".json")) {
                path = path + ".json";
            }
            var presetName = "";
            var lastSlash = path.lastIndexOf("/");
            var fileName = lastSlash >= 0 ? path.substring(lastSlash + 1) : path;
            var dotIndex = fileName.lastIndexOf(".");
            if (dotIndex > 0) {
                presetName = fileName.substring(0, dotIndex);
            } else {
                presetName = fileName;
            }
            var ok = editorController.saveShaderPreset(path, root.pendingShaderId, root.pendingParams || {}, presetName);
            if (ok) {
                root.presetErrorMessage = "";
            }
            root.restoreShaderPreview();
        }
        onRejected: function() { root.restoreShaderPreview(); }
    }

    FileDialog {
        id: loadPresetDialog

        title: i18nc("@title:window", "Load Shader Preset")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)"), i18nc("@item:inlistbox", "All files (*)")]
        fileMode: FileDialog.OpenFile

        onAccepted: {
            if (!editorController) return;
            var path = filePathFromUrl(selectedFile);
            var result = editorController.loadShaderPreset(path);
            if (result && result.shaderId) {
                root.pendingShaderId = result.shaderId;
                root.pendingParams = result.shaderParams || {};
                root.presetErrorMessage = "";
            }
            root.restoreShaderPreview();
        }
        onRejected: function() { root.restoreShaderPreview(); }
    }
}
