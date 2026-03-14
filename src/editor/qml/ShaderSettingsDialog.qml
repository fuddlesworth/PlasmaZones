// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

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
    // ═══════════════════════════════════════════════════════════════════════
    // CONSTANTS
    // ═══════════════════════════════════════════════════════════════════════
    readonly property int colorButtonSize: Kirigami.Units.gridUnit * 3
    // Theme colors cached at dialog level for use in dynamically loaded Components
    // Use disabledTextColor with reduced opacity as separator color
    readonly property color themeSeparatorColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
    readonly property int sliderValueLabelWidth: Kirigami.Units.gridUnit * 4
    readonly property int colorLabelWidth: Kirigami.Units.gridUnit * 7
    // ═══════════════════════════════════════════════════════════════════════
    // PENDING STATE (buffered until Apply is clicked)
    // ═══════════════════════════════════════════════════════════════════════
    property var pendingParams: ({
    })
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
        if (!editorController)
            return null;

        var shaders = editorController.availableShaders;
        if (!shaders || !pendingShaderId)
            return null;

        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i] && shaders[i].id === pendingShaderId)
                return shaders[i];

        }
        return null;
    }
    readonly property var shaderParams: currentShaderInfo ? (currentShaderInfo.parameters || []) : []
    // Shaders sorted by name for the dropdown (avoids arbitrary QHash order)
    readonly property var sortedShaders: {
        var shaders = editorController ? editorController.availableShaders : [];
        if (!shaders || shaders.length === 0)
            return [];

        var arr = [];
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i])
                arr.push(shaders[i]);

        }
        arr.sort(function(a, b) {
            var na = (a && a.name !== undefined) ? String(a.name) : "";
            var nb = (b && b.name !== undefined) ? String(b.name) : "";
            return na.localeCompare(nb);
        });
        return arr;
    }
    // Shaders grouped by category for the cascading menu.
    // Supports "/" path separators for nested submenus (e.g. "Audio/Reactive").
    // Returns: [{ name, shaders (direct), subcategories: [{ name, shaders }] }]
    readonly property var shaderCategoryTree: {
        var shaders = root.sortedShaders;
        var tree = {
        }; // { topName: { direct: [], subcats: { subName: [] } } }
        var uncategorized = [];
        for (var i = 0; i < shaders.length; i++) {
            var s = shaders[i];
            var cat = (s.category || "").trim();
            if (cat === "") {
                uncategorized.push(s);
                continue;
            }
            var slashIdx = cat.indexOf("/");
            var top = slashIdx >= 0 ? cat.substring(0, slashIdx).trim() : cat;
            var sub = slashIdx >= 0 ? cat.substring(slashIdx + 1).trim() : "";
            if (top === "") {
                uncategorized.push(s);
                continue;
            }
            if (!tree[top])
                tree[top] = {
                "direct": [],
                "subcats": {
                }
            };

            if (sub === "") {
                tree[top].direct.push(s);
            } else {
                if (!tree[top].subcats[sub])
                    tree[top].subcats[sub] = [];

                tree[top].subcats[sub].push(s);
            }
        }
        // Convert to sorted array
        var keys = Object.keys(tree);
        keys.sort(function(a, b) {
            return a.localeCompare(b);
        });
        var categories = [];
        for (var k = 0; k < keys.length; k++) {
            var node = tree[keys[k]];
            var subKeys = Object.keys(node.subcats);
            subKeys.sort(function(a, b) {
                return a.localeCompare(b);
            });
            var subcategories = [];
            for (var s = 0; s < subKeys.length; s++) subcategories.push({
                "name": subKeys[s],
                "shaders": node.subcats[subKeys[s]]
            })
            categories.push({
                "name": keys[k],
                "shaders": node.direct,
                "subcategories": subcategories
            });
        }
        return {
            "categories": categories,
            "uncategorized": uncategorized
        };
    }
    readonly property var shaderCategories: shaderCategoryTree.categories || []
    readonly property var uncategorizedShaders: shaderCategoryTree.uncategorized || []
    // Computed property: parameters grouped by their "group" field
    readonly property var parameterGroups: buildParameterGroups(shaderParams)
    // Pause/resume local preview animation when app loses/gains focus
    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive
    // Local shader preview — static config rebuilt on shader/params/zones change
    property var previewShaderConfig: null
    // {bufferShaderPaths, bufferFeedback, bufferScale, bufferWrap, useWallpaper, zones, shaderParams} (shaderSource set imperatively)
    // Cached shader metadata (static per shader — avoid D-Bus call on every param change)
    property var cachedShaderInfoForPreview: null
    property string cachedShaderInfoId: ""
    // Animation state for local preview (bound directly, not via config object)
    property real previewITime: 0
    property real previewLastTime: 0
    property real previewTimeDelta: 0.016
    property int previewFrame: 0

    // Translate shader category names for display
    function translateCategory(cat) {
        switch (cat) {
        case "3D":
            return i18nc("@item:inmenu shader category", "3D");
        case "Audio Visualizer":
            return i18nc("@item:inmenu shader category", "Audio Visualizer");
        case "Branded":
            return i18nc("@item:inmenu shader category", "Branded");
        case "Cyberpunk":
            return i18nc("@item:inmenu shader category", "Cyberpunk");
        case "Energy":
            return i18nc("@item:inmenu shader category", "Energy");
        case "Organic":
            return i18nc("@item:inmenu shader category", "Organic");
        default:
            return cat;
        }
    }

    function hideShaderPreview() {
        previewAnimationTimer.stop();
        previewShaderConfig = null;
        shaderPreview.shaderSource = "";
        if (editorController)
            editorController.stopAudioCapture();

    }

    // Build local preview config from shader info + translated params + zones
    function updateLocalShaderPreview() {
        if (!previewAllowed) {
            root.hideShaderPreview();
            return ;
        }
        if (!editorController || !root.hasShaderEffect || root.pendingShaderId === root.noneShaderId) {
            root.hideShaderPreview();
            return ;
        }
        if (!previewContainer.visible || previewBackground.width <= 0 || previewBackground.height <= 0)
            return ;

        var w = Math.max(1, Math.floor(previewBackground.width));
        var h = Math.max(1, Math.floor(previewBackground.height));
        var zones = editorController.zonesForShaderPreview(w, h);
        var params = editorController.translateShaderParams(root.pendingShaderId, root.pendingParams || {
        });
        // Cache shader metadata per shader ID (avoids D-Bus call on every param change)
        if (root.cachedShaderInfoId !== root.pendingShaderId) {
            root.cachedShaderInfoForPreview = editorController.getShaderInfo(root.pendingShaderId);
            root.cachedShaderInfoId = root.pendingShaderId;
        }
        var info = root.cachedShaderInfoForPreview;
        if (!info || !info.shaderUrl) {
            root.hideShaderPreview();
            return ;
        }
        // Build labels texture (zone numbers) for the preview
        var labelsImg = editorController.buildLabelsTexture(zones, w, h);
        // Load wallpaper texture if shader uses it
        var useWallpaper = info.wallpaper || false;
        var wallpaperImg = useWallpaper ? editorController.loadWallpaperTexture() : null;
        var bsPaths = info.bufferShaderPaths || [];
        var shaderUrl = info.shaderUrl || "";
        // Set config WITHOUT shaderSource — shaderSource is set imperatively
        // after all config bindings have evaluated (see Qt.callLater below).
        // This mirrors the daemon's applyShaderInfoToWindow() which sets
        // shaderSource LAST because setShaderSource() triggers compilation
        // that references bufferShaderPaths, zones, and params.
        previewShaderConfig = {
            "bufferShaderPaths": (bsPaths.length > 0) ? Array.from(bsPaths) : (info.bufferShaderPath ? [info.bufferShaderPath] : []),
            "bufferFeedback": info.bufferFeedback || false,
            "bufferScale": info.bufferScale !== undefined ? info.bufferScale : 1,
            "bufferWrap": info.bufferWrap || "clamp",
            "useWallpaper": useWallpaper,
            "zones": zones,
            "shaderParams": params,
            "labelsTexture": labelsImg,
            "wallpaperTexture": wallpaperImg
        };
        // Set shaderSource AFTER bindings settle — Qt.callLater defers to
        // the next event loop iteration, guaranteeing all property bindings
        // from the config assignment above have been evaluated first.
        // Guard against stale URL from rapid shader switching: only apply if
        // the pending shader ID still matches when the deferred call fires.
        var capturedShaderId = root.pendingShaderId;
        Qt.callLater(function() {
            if (root.pendingShaderId === capturedShaderId)
                shaderPreview.shaderSource = shaderUrl;

        });
        if (!previewAnimationTimer.running) {
            previewLastTime = Date.now() / 1000;
            previewITime = 0;
            previewTimeDelta = 0.016;
            previewFrame = 0;
            previewAnimationTimer.start();
            // Start CAVA audio capture once when preview first activates
            if (editorController)
                editorController.startAudioCapture();

        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STATE MANAGEMENT FUNCTIONS
    // ═══════════════════════════════════════════════════════════════════════
    function initializePendingState() {
        if (!editorController)
            return ;

        pendingShaderId = editorController.currentShaderId || "";
        var current = editorController.currentShaderParams || {
        };
        var copy = {
        };
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
            pendingParams = {
            };
            return ;
        }
        pendingParams = extractDefaults(params);
    }

    // Helper to get shader parameters directly by ID, avoiding computed property timing issues
    function getShaderParamsById(shaderId) {
        if (!editorController || !shaderId)
            return [];

        var shaders = editorController.availableShaders;
        if (!shaders)
            return [];

        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i] && shaders[i].id === shaderId)
                return shaders[i].parameters || [];

        }
        return [];
    }

    function setPendingParam(paramId, value) {
        var copy = {
        };
        for (var key in pendingParams) {
            copy[key] = pendingParams[key];
        }
        copy[paramId] = value;
        pendingParams = copy;
    }

    function parameterValue(paramId, fallback) {
        if (pendingParams && pendingParams[paramId] !== undefined)
            return pendingParams[paramId];

        return fallback;
    }

    function applyChanges() {
        if (!editorController)
            return ;

        if (pendingShaderId !== editorController.currentShaderId) {
            // Shader switched: atomic undo of ID + params (single Ctrl+Z)
            editorController.switchShader(pendingShaderId, pendingParams);
        } else {
            // Same shader: apply individual param changes (supports undo merge for slider drags)
            var currentParams = editorController.currentShaderParams || {
            };
            for (var paramId in pendingParams) {
                if (pendingParams[paramId] !== currentParams[paramId])
                    editorController.setShaderParameter(paramId, pendingParams[paramId]);

            }
        }
    }

    function resetToDefaults() {
        if (!editorController)
            return ;

        pendingParams = extractDefaults(shaderParams);
    }

    function randomizeParameters() {
        if (!editorController || shaderParams.length === 0)
            return ;

        var randomized = {
        };
        for (var i = 0; i < shaderParams.length; i++) {
            var param = shaderParams[i];
            if (!param || param.id === undefined)
                continue;

            var value;
            switch (param.type) {
            case "float":
                var minF = param.min !== undefined ? param.min : 0;
                var maxF = param.max !== undefined ? param.max : 1;
                value = minF + Math.random() * (maxF - minF);
                // Round to step if defined
                if (param.step !== undefined && param.step > 0)
                    value = Math.round(value / param.step) * param.step;

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
                value = "#" + r.toString(16).padStart(2, "0") + g.toString(16).padStart(2, "0") + b.toString(16).padStart(2, "0");
                break;
            case "image":
                // Preserve current image path (don't randomize file paths)
                value = root.parameterValue(param.id, param.default);
                break;
            default:
                // Unknown type - use default if available
                value = param.default;
                break;
            }
            if (value !== undefined)
                randomized[param.id] = value;

        }
        pendingParams = randomized;
    }

    // Extract default values from parameter definitions
    function extractDefaults(params) {
        var defaults = {
        };
        if (!params)
            return defaults;

        for (var i = 0; i < params.length; i++) {
            var param = params[i];
            if (param && param.id !== undefined && param.default !== undefined)
                defaults[param.id] = param.default;

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
        // Use sortedShaders (alphabetical by name) so the pick is deterministic
        var shaders = root.sortedShaders;
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i] && shaders[i].id && shaders[i].id !== noneShaderId)
                return shaders[i].id;

        }
        return noneShaderId;
    }

    function filePathFromUrl(url) {
        if (!url)
            return "";

        return url.toString().replace(/^file:\/\/+/, "/");
    }

    function preparePresetDialog(dialog) {
        if (editorController) {
            var dir = editorController.shaderPresetDirectory();
            if (dir && dir.length > 0)
                dialog.currentFolder = Qt.resolvedUrl("file://" + dir);

        }
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
            result.push({
                "name": name,
                "params": groupMap[name]
            });
        }
        return result;
    }

    title: i18nc("@title:window", "Shader Settings")
    preferredWidth: root.hasShaderEffect ? Kirigami.Units.gridUnit * 56 : Kirigami.Units.gridUnit * 32
    preferredHeight: root.hasShaderEffect ? Kirigami.Units.gridUnit * 32 : Kirigami.Units.gridUnit * 10
    padding: Kirigami.Units.largeSpacing
    // ═══════════════════════════════════════════════════════════════════════
    // LIFECYCLE
    // ═══════════════════════════════════════════════════════════════════════
    onOpened: {
        previewAllowed = true;
        presetErrorMessage = "";
        initializePendingState();
        expandedGroupIndex = 0;
        Qt.callLater(root.updateLocalShaderPreview);
    }
    onAppActiveChanged: {
        if (!root.visible || !root.hasShaderEffect)
            return ;

        if (appActive) {
            if (root.previewShaderConfig) {
                root.previewLastTime = Date.now() / 1000;
                previewAnimationTimer.start();
                // Re-check CAVA on focus — user may have toggled it in KCM
                if (editorController)
                    editorController.startAudioCapture();

            }
        } else {
            previewAnimationTimer.stop();
        }
    }
    onClosed: {
        previewAllowed = false;
        debouncePreviewUpdate.stop();
        root.hideShaderPreview();
        cachedShaderInfoForPreview = null;
        cachedShaderInfoId = "";
    }
    onPendingShaderIdChanged: {
        if (visible)
            initializePendingParamsForShader();

        debouncePreviewUpdate.restart();
    }
    onPendingParamsChanged: debouncePreviewUpdate.restart()
    standardButtons: Kirigami.Dialog.NoButton

    Connections {
        function onShaderPresetLoadFailed(error) {
            root.presetErrorMessage = error;
        }

        function onShaderPresetSaveFailed(error) {
            root.presetErrorMessage = error;
        }

        target: editorController
        enabled: editorController !== null
    }

    Timer {
        id: debouncePreviewUpdate

        interval: 150
        repeat: false
        onTriggered: root.updateLocalShaderPreview()
    }

    // Local animation timer for shader preview (replaces daemon-side 60fps timer)
    Timer {
        id: previewAnimationTimer

        interval: 16 // ~60fps
        repeat: true
        onTriggered: {
            var now = Date.now() / 1000;
            var delta = Math.min(now - root.previewLastTime, 0.1);
            root.previewLastTime = now;
            root.previewITime += delta;
            root.previewTimeDelta = delta;
            root.previewFrame = (root.previewFrame + 1) % 1e+09;
        }
    }

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
                            if (root.pendingShaderId === root.noneShaderId)
                                root.pendingShaderId = root.firstEffectId();

                        } else {
                            root.pendingShaderId = root.noneShaderId;
                        }
                    }
                }

                Button {
                    id: shaderMenuButton

                    readonly property string displayText: {
                        var info = root.currentShaderInfo;
                        return info ? info.name : i18nc("@action:button", "Select shader...");
                    }

                    Kirigami.FormData.label: i18nc("@label", "Shader:")
                    enabled: root.hasShaderEffect
                    Layout.fillWidth: true
                    onClicked: shaderCategoryMenu.popup(shaderMenuButton, 0, shaderMenuButton.height)
                    Accessible.name: displayText
                    ToolTip.text: i18nc("@info:tooltip", "Choose a shader effect from categorized list")
                    ToolTip.visible: hovered && !shaderCategoryMenu.visible
                    ToolTip.delay: Kirigami.Units.toolTipDelay

                    // Exclusive action group ensures only one shader is checked at a time
                    ActionGroup {
                        id: shaderActionGroup

                        exclusive: true
                    }

                    Menu {
                        id: shaderCategoryMenu

                        // Category submenus (with support for nested subcategories via "/")
                        Instantiator {
                            id: categoryInstantiator

                            model: root.shaderCategories
                            onObjectAdded: (index, object) => {
                                return shaderCategoryMenu.insertMenu(index, object);
                            }
                            onObjectRemoved: (index, object) => {
                                return shaderCategoryMenu.removeMenu(object);
                            }

                            delegate: Menu {
                                id: categorySubMenu

                                required property var modelData

                                title: root.translateCategory(modelData.name)

                                // Direct shaders in this category (no subcategory)
                                Instantiator {
                                    model: modelData.shaders
                                    onObjectAdded: (index, object) => {
                                        return categorySubMenu.insertItem(index, object);
                                    }
                                    onObjectRemoved: (index, object) => {
                                        return categorySubMenu.removeItem(object);
                                    }

                                    delegate: MenuItem {
                                        required property var modelData

                                        text: modelData.name
                                        checkable: true
                                        checked: modelData.id === root.pendingShaderId
                                        ActionGroup.group: shaderActionGroup
                                        onTriggered: root.pendingShaderId = modelData.id
                                    }

                                }

                                // Subcategory submenus (from "/" path splits)
                                Instantiator {
                                    model: modelData.subcategories || []
                                    onObjectAdded: (index, object) => {
                                        return categorySubMenu.insertMenu(categorySubMenu.count, object);
                                    }
                                    onObjectRemoved: (index, object) => {
                                        return categorySubMenu.removeMenu(object);
                                    }

                                    delegate: Menu {
                                        id: subCategoryMenu

                                        required property var modelData

                                        title: root.translateCategory(modelData.name)

                                        Instantiator {
                                            model: subCategoryMenu.modelData.shaders
                                            onObjectAdded: (index, object) => {
                                                return subCategoryMenu.insertItem(index, object);
                                            }
                                            onObjectRemoved: (index, object) => {
                                                return subCategoryMenu.removeItem(object);
                                            }

                                            delegate: MenuItem {
                                                required property var modelData

                                                text: modelData.name
                                                checkable: true
                                                checked: modelData.id === root.pendingShaderId
                                                ActionGroup.group: shaderActionGroup
                                                onTriggered: root.pendingShaderId = modelData.id
                                            }

                                        }

                                    }

                                }

                            }

                        }

                        // Separator before uncategorized shaders (if any exist)
                        MenuSeparator {
                            visible: root.uncategorizedShaders.length > 0 && root.shaderCategories.length > 0
                        }

                        // Uncategorized shaders appear at the top level
                        Instantiator {
                            model: root.uncategorizedShaders
                            onObjectAdded: (index, object) => {
                                return shaderCategoryMenu.insertItem(shaderCategoryMenu.count, object);
                            }
                            onObjectRemoved: (index, object) => {
                                return shaderCategoryMenu.removeItem(object);
                            }

                            delegate: MenuItem {
                                required property var modelData

                                text: modelData.name
                                checkable: true
                                checked: modelData.id === root.pendingShaderId
                                ActionGroup.group: shaderActionGroup
                                onTriggered: root.pendingShaderId = modelData.id
                            }

                        }

                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            Layout.fillWidth: true
                            text: shaderMenuButton.displayText
                            elide: Text.ElideRight
                            color: shaderMenuButton.enabled ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                        }

                        Kirigami.Icon {
                            source: "arrow-down"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            Layout.alignment: Qt.AlignVCenter
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
                Layout.preferredWidth: 0 // Don't expand parent based on content
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
                        if (!root.currentShaderInfo)
                            return i18nc("@info:placeholder", "No description available");

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
                        if (!root.currentShaderInfo)
                            return "";

                        var info = root.currentShaderInfo;
                        var parts = [];
                        if (info.author)
                            parts.push(i18nc("@info shader author", "by %1", info.author));

                        if (info.version)
                            parts.push(i18nc("@info shader version", "v%1", info.version));

                        if (info.isUserShader)
                            parts.push(i18nc("@info user-installed shader", "(User shader)"));

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

                        contentComponent: Component {
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
                        savePresetDialog.open();
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

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
                // shaderSource is set imperatively from updateLocalShaderPreview()
                // via Qt.callLater — NOT via binding — to guarantee it runs after
                // all other config properties have been applied.

                id: previewBackground

                readonly property var cfg: root.previewShaderConfig || ({
                })
                // Mouse tracking for zone hover highlight in preview
                property point previewMouse: Qt.point(-1, -1)
                readonly property int previewHoveredZone: {
                    var mouse = previewBackground.previewMouse;
                    if (mouse.x < 0 || mouse.y < 0)
                        return -1;

                    var zones = previewBackground.cfg.zones || [];
                    for (var i = 0; i < zones.length; i++) {
                        var z = zones[i];
                        var zx = (z.x !== undefined) ? z.x : 0;
                        var zy = (z.y !== undefined) ? z.y : 0;
                        var zw = (z.width !== undefined) ? z.width : 0;
                        var zh = (z.height !== undefined) ? z.height : 0;
                        if (mouse.x >= zx && mouse.x < zx + zw && mouse.y >= zy && mouse.y < zy + zh)
                            return i;

                    }
                    return -1;
                }

                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                color: Kirigami.Theme.backgroundColor
                radius: Kirigami.Units.smallSpacing
                clip: true
                onWidthChanged: debouncePreviewUpdate.restart()
                onHeightChanged: debouncePreviewUpdate.restart()

                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    onPositionChanged: function(mouse) {
                        previewBackground.previewMouse = Qt.point(mouse.x, mouse.y);
                    }
                    onExited: {
                        previewBackground.previewMouse = Qt.point(-1, -1);
                    }
                }

                // Local shader renderer — replaces daemon-side LayerShell overlay
                // shaderSource is set imperatively via Qt.callLater in
                // updateLocalShaderPreview() to guarantee it runs after all
                // config bindings (bufferShaderPaths, zones, params) have settled.
                ZoneShaderItem {
                    id: shaderPreview

                    anchors.fill: parent
                    visible: root.previewShaderConfig !== null
                    // Render to a private layer FBO so multipass shaders' beginPass(rt)
                    // clears only this layer — not the entire editor window surface.
                    layer.enabled: true
                    // Our render node already outputs correct top-down orientation;
                    // disable default MirrorVertically to prevent double-flip.
                    layer.textureMirroring: ShaderEffectSource.NoMirroring
                    // All auxiliary props BEFORE shaderSource
                    bufferShaderPaths: previewBackground.cfg.bufferShaderPaths || []
                    bufferFeedback: previewBackground.cfg.bufferFeedback || false
                    bufferScale: previewBackground.cfg.bufferScale !== undefined ? previewBackground.cfg.bufferScale : 1
                    bufferWrap: previewBackground.cfg.bufferWrap || "clamp"
                    zones: previewBackground.cfg.zones || []
                    shaderParams: previewBackground.cfg.shaderParams || ({
                    })
                    useWallpaper: previewBackground.cfg.useWallpaper || false
                    // Timing bound directly to root properties for per-frame updates
                    iTime: root.previewITime
                    iTimeDelta: root.previewTimeDelta
                    iFrame: root.previewFrame
                    iResolution: Qt.size(width, height)
                    iMouse: previewBackground.previewMouse
                    hoveredZoneIndex: previewBackground.previewHoveredZone
                    audioSpectrum: editorController ? editorController.audioSpectrum : []
                }

                // QImage bindings use Binding with `when` guard to avoid passing
                // undefined/null to C++ setters (can crash during teardown)
                Binding {
                    target: shaderPreview
                    property: "labelsTexture"
                    value: previewBackground.cfg.labelsTexture
                    when: previewBackground.cfg.labelsTexture !== undefined && previewBackground.cfg.labelsTexture !== null
                }

                Binding {
                    target: shaderPreview
                    property: "wallpaperTexture"
                    value: previewBackground.cfg.wallpaperTexture
                    when: previewBackground.cfg.wallpaperTexture !== undefined && previewBackground.cfg.wallpaperTexture !== null
                }

                // Fallback message when no shader is rendering
                Label {
                    anchors.centerIn: parent
                    visible: root.previewShaderConfig === null && root.hasShaderEffect
                    text: i18nc("@info:placeholder", "Loading preview…")
                    opacity: 0.5
                }

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
            if (paramId)
                root.setPendingParam(paramId, selectedColor.toString());

        }
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
            if (!editorController)
                return ;

            var path = filePathFromUrl(selectedFile);
            if (!path.toLowerCase().endsWith(".json"))
                path = path + ".json";

            var presetName = "";
            var lastSlash = path.lastIndexOf("/");
            var fileName = lastSlash >= 0 ? path.substring(lastSlash + 1) : path;
            var dotIndex = fileName.lastIndexOf(".");
            if (dotIndex > 0)
                presetName = fileName.substring(0, dotIndex);
            else
                presetName = fileName;
            var ok = editorController.saveShaderPreset(path, root.pendingShaderId, root.pendingParams || {
            }, presetName);
            if (ok)
                root.presetErrorMessage = "";

        }
    }

    FileDialog {
        id: loadPresetDialog

        title: i18nc("@title:window", "Load Shader Preset")
        nameFilters: [i18nc("@item:inlistbox", "JSON files (*.json)"), i18nc("@item:inlistbox", "All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (!editorController)
                return ;

            var path = filePathFromUrl(selectedFile);
            var result = editorController.loadShaderPreset(path);
            if (result && result.shaderId) {
                root.pendingShaderId = result.shaderId;
                root.pendingParams = result.shaderParams || {
                };
                root.presetErrorMessage = "";
            }
        }
    }

}
