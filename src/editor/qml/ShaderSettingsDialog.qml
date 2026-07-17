// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import PlasmaZones 1.0
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief Shader settings dialog
 *
 * Provides a consistent UX for configuring shader effects on zone overlays.
 * Side-by-side layout: settings on the left, live preview on the right.
 *
 * The picker, parameter rows, accordion sections and lock/randomize
 * toolbar live in `org.plasmazones.common` (PZCommon.CategoryMenuButton /
 * ParameterEditor) so the animation-settings page can reuse them.
 * This dialog owns the editor-specific bits: the live preview pane, the
 * shader-preset load/save flow, color/image platform dialogs, and the
 * pending-state buffering until "Apply" is clicked.
 */
Kirigami.Dialog {
    // No explicit `Qt.callLater(updateLocalShaderPreview)` —
    // `initializePendingState` mutates `pendingShaderId` /
    // `pendingParams`, which fire the existing change handlers and
    // restart `debouncePreviewUpdate`. The 150ms debounce gives the
    // preview pane time to receive its layout geometry before the
    // shader pipeline tries to read width/height.

    id: root

    required property var editorController
    // EditorWindow reference, needed for the shared urlToLocalPath helper
    required property var editorWindow
    // ═══════════════════════════════════════════════════════════════════════
    // PENDING STATE (buffered until Apply is clicked)
    // ═══════════════════════════════════════════════════════════════════════
    property var pendingParams: ({})
    property var lockedParams: ({})
    property string pendingShaderId: ""
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
    // Pause/resume local preview animation when app loses/gains focus
    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive
    // Local shader preview — static config rebuilt on shader/params/zones change
    property var previewShaderConfig: null
    // Cached shader metadata (static per shader — avoid D-Bus call on every param change)
    property var cachedShaderInfoForPreview: null
    property string cachedShaderInfoId: ""
    // The generated p_<id> preamble depends only on shaderId, not params — cache
    // it with the shader info so it isn't recomputed (a D-Bus shaderInfo round-trip)
    // on every param edit.
    property string cachedShaderParamPreamble: ""
    // Animation state for local preview (bound directly, not via config object)
    property real previewITime: 0
    property real previewLastTime: 0
    property real previewTimeDelta: 0.016
    property int previewFrame: 0

    // Build a small-theme font carrying only the size dimension the theme font
    // actually holds (the other reads -1, which Qt.font warns on). Reads
    // Kirigami.Theme.smallFont inside the caller's binding, so the binding
    // stays reactive to theme font changes. Local twin of the settings app's
    // FontUtils.js (a different QML module, so the library is not importable
    // here).
    function smallFontWith(extra) {
        const base = Kirigami.Theme.smallFont;
        const props = Object.assign({
            family: base.family
        }, extra);
        if (base.pixelSize > 0)
            props.pixelSize = base.pixelSize;
        else
            props.pointSize = base.pointSize;
        return Qt.font(props);
    }

    function hideShaderPreview() {
        previewAnimationTimer.stop();
        previewShaderConfig = null;
        // Setting previewShaderConfig = null deactivates the Loader, which
        // destroys the ZoneShaderItem and releases all RHI resources cleanly.
        if (editorController)
            editorController.stopAudioCapture();
    }

    function restoreShaderPreview() {
        Qt.callLater(root.updateLocalShaderPreview);
    }

    function updateLocalShaderPreview() {
        if (!previewAllowed) {
            root.hideShaderPreview();
            return;
        }
        if (!editorController || !root.hasShaderEffect || root.pendingShaderId === root.noneShaderId) {
            root.hideShaderPreview();
            return;
        }
        if (!previewContainer.visible || previewBackground.width <= 0 || previewBackground.height <= 0)
            return;

        var w = Math.max(1, Math.floor(previewBackground.width));
        var h = Math.max(1, Math.floor(previewBackground.height));
        var zones = editorController.zonesForShaderPreview(w, h);
        var params = editorController.translateShaderParams(root.pendingShaderId, root.pendingParams || {});
        if (root.cachedShaderInfoId !== root.pendingShaderId) {
            root.cachedShaderInfoForPreview = editorController.getShaderInfo(root.pendingShaderId);
            root.cachedShaderParamPreamble = editorController.shaderParamPreamble(root.pendingShaderId);
            root.cachedShaderInfoId = root.pendingShaderId;
        }
        var info = root.cachedShaderInfoForPreview;
        if (!info || !info.shaderUrl) {
            root.hideShaderPreview();
            return;
        }
        var labelsImg = editorController.buildLabelsTexture(zones, w, h);
        var useWallpaper = info.wallpaper || false;
        var wallpaperImg = useWallpaper ? editorController.loadWallpaperTexture() : null;
        var bsPaths = info.bufferShaderPaths || [];
        var shaderUrl = info.shaderUrl || "";
        previewShaderConfig = {
            "shaderUrl": shaderUrl,
            "paramPreamble": root.cachedShaderParamPreamble,
            "bufferShaderPaths": (bsPaths.length > 0) ? Array.from(bsPaths) : (info.bufferShaderPath ? [info.bufferShaderPath] : []),
            "bufferFeedback": info.bufferFeedback || false,
            "bufferScale": info.bufferScale !== undefined ? info.bufferScale : 1,
            "bufferWrap": info.bufferWrap || "clamp",
            "bufferWraps": info.bufferWraps || [],
            "bufferFilter": info.bufferFilter || "linear",
            "bufferFilters": info.bufferFilters || [],
            "useWallpaper": useWallpaper,
            "useDepthBuffer": info.depthBuffer || false,
            "zones": zones,
            "shaderParams": params,
            "labelsTexture": labelsImg,
            "wallpaperTexture": wallpaperImg
        };
        if (!previewAnimationTimer.running) {
            previewLastTime = Date.now() / 1000;
            previewITime = 0;
            previewTimeDelta = 0.016;
            previewFrame = 0;
            previewAnimationTimer.start();
            if (editorController)
                editorController.startAudioCapture();
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // STATE MANAGEMENT FUNCTIONS
    // ═══════════════════════════════════════════════════════════════════════
    function initializePendingState() {
        if (!editorController)
            return;

        // Set `pendingShaderId` first — that fires `onPendingShaderIdChanged`
        // which calls `initializePendingParamsForShader()` and overwrites
        // `pendingParams` with schema defaults. We then assign
        // `pendingParams` from the LIVE controller params, which is the
        // last-write-wins result and what the user expects on open.
        pendingShaderId = editorController.currentShaderId || "";
        pendingParams = Object.assign({}, editorController.currentShaderParams || {});
        lockedParams = {};
    }

    function initializePendingParamsForShader() {
        var params = getShaderParamsById(pendingShaderId);
        if (!params || params.length === 0) {
            pendingParams = {};
            return;
        }
        pendingParams = extractDefaults(params);
    }

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
        var next = Object.assign({}, pendingParams || {});
        next[paramId] = value;
        pendingParams = next;
    }

    function setParamLocked(paramId, locked) {
        lockedParams = paramEditor.lockedAfterToggle(paramId, locked);
    }

    function toggleAllLocks(locked) {
        lockedParams = paramEditor.lockedAfterAllToggle(locked);
    }

    function applyChanges() {
        if (!editorController)
            return;

        if (pendingShaderId !== editorController.currentShaderId) {
            // Shader switched: atomic undo of ID + params (single Ctrl+Z).
            // Switching INTO the None shader strips the param map — the
            // None shader has no schema, so carrying the previous shader's
            // params would persist a stale map the daemon couldn't validate.
            var nextParams = (pendingShaderId === noneShaderId) ? ({}) : pendingParams;
            editorController.switchShader(pendingShaderId, nextParams);
        } else {
            var currentParams = editorController.currentShaderParams || {};
            for (var paramId in pendingParams) {
                if (pendingParams[paramId] !== currentParams[paramId])
                    editorController.setShaderParameter(paramId, pendingParams[paramId]);
            }
        }
    }

    function resetToDefaults() {
        if (!editorController)
            return;

        pendingParams = extractDefaults(shaderParams);
    }

    function randomizeParameters() {
        if (!editorController || shaderParams.length === 0)
            return;

        pendingParams = paramEditor.computeRandomized();
    }

    function extractDefaults(params) {
        var defaults = {};
        if (!params)
            return defaults;

        for (var i = 0; i < params.length; i++) {
            var param = params[i];
            if (param && param.id !== undefined && param.default !== undefined)
                defaults[param.id] = param.default;
        }
        return defaults;
    }

    function openColorDialog(paramId, paramName, currentColor) {
        shaderColorDialog.selectedColor = currentColor;
        shaderColorDialog.paramId = paramId;
        shaderColorDialog.paramName = paramName;
        shaderColorDialog.open();
    }

    // Owned by the dialog root (not the delegate row) so the platform
    // FileDialog wrapper outlives any row that gets destroyed by a
    // Repeater model change while it's still open.
    function openImageDialog(paramId) {
        shaderImageDialog.paramId = paramId;
        root.hideShaderPreview();
        shaderImageDialog.open();
    }

    function firstEffectId() {
        if (!editorController)
            return noneShaderId;

        var shaders = editorController.availableShaders;
        // Sort alphabetically so the pick is deterministic across registry orderings.
        var arr = [];
        for (var i = 0; i < shaders.length; i++) {
            if (shaders[i])
                arr.push(shaders[i]);
        }
        arr.sort(function (a, b) {
            return String(a.name || "").localeCompare(String(b.name || ""));
        });
        for (var j = 0; j < arr.length; j++) {
            if (arr[j].id && arr[j].id !== noneShaderId)
                return arr[j].id;
        }
        return noneShaderId;
    }

    function filePathFromUrl(url) {
        // Single URL→path implementation shared with EditorWindow's
        // import/export dialogs (handles %-encoded characters and both
        // file:// and file:/// forms).
        return root.editorWindow.urlToLocalPath(url);
    }

    function preparePresetDialog(dialog) {
        if (editorController) {
            var dir = editorController.shaderPresetDirectory();
            // `encodeURI` percent-encodes spaces and unicode while preserving
            // path separators; the residual replaces cover `#` and `?`, which
            // encodeURI leaves alone but the file:// URL would parse as
            // fragment/query delimiters (same pattern as ShaderBrowserCard).
            if (dir && dir.length > 0)
                dialog.currentFolder = Qt.resolvedUrl("file://" + encodeURI(dir).replace(/#/g, "%23").replace(/\?/g, "%3F"));
        }
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
        // Re-sync imperatively: the checkbox's `checked` binding is severed
        // by the first user toggle (QQC2 interactive-property behavior), so
        // reopening the dialog must push the actual state back in.
        enableEffectCheck.checked = root.hasShaderEffect;
    }
    onAppActiveChanged: {
        if (!root.visible || !root.hasShaderEffect)
            return;

        if (appActive) {
            if (root.previewShaderConfig) {
                root.previewLastTime = Date.now() / 1000;
                previewAnimationTimer.start();
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
        cachedShaderParamPreamble = "";
        cachedShaderInfoId = "";
    }
    onPendingShaderIdChanged: {
        if (visible)
            initializePendingParamsForShader();

        lockedParams = {};
        debouncePreviewUpdate.restart();
        // Re-sync imperatively: the checkbox's `checked` binding is severed
        // by the first user toggle (QQC2 interactive-property behavior), so
        // programmatic shader changes (e.g. loading a preset that crosses the
        // none/effect boundary) must push the actual state back in.
        enableEffectCheck.checked = root.hasShaderEffect;
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

        // ═══════════════════════════════════════════════════════════════
        // LEFT: Settings panel
        // ═══════════════════════════════════════════════════════════════
        ColumnLayout {
            Layout.fillHeight: root.hasShaderEffect
            Layout.preferredWidth: Kirigami.Units.gridUnit * 28
            Layout.minimumWidth: Kirigami.Units.gridUnit * 28
            spacing: Kirigami.Units.largeSpacing

            // ── Effect selection (enable + picker) ─────────────────────
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
                        // Re-derive from the model: with no effect shaders registered the assignments above are a no-op and the box must not latch checked.
                        checked = root.hasShaderEffect;
                    }
                }

                PZCommon.CategoryMenuButton {
                    Kirigami.FormData.label: i18nc("@label", "Shader:")
                    enabled: root.hasShaderEffect
                    Layout.fillWidth: true
                    items: editorController ? editorController.availableShaders : []
                    currentId: root.pendingShaderId
                    noneId: root.noneShaderId
                    placeholderText: i18nc("@action:button", "Select shader…")
                    onSelected: function (id) {
                        // Hide preview before the switch so RHI resources
                        // are released cleanly before the new shader spins
                        // up. NVIDIA EGL 595.x corrupts the heap on
                        // in-place pipeline swaps. Reassigning
                        // `pendingShaderId` then fires `onPendingShaderIdChanged`
                        // which restarts the debounce timer; the timer
                        // re-runs `updateLocalShaderPreview()` once the
                        // ID has settled — no explicit restore needed
                        // here (a manual `restoreShaderPreview()` would
                        // double-fire the rebuild).
                        root.hideShaderPreview();
                        root.pendingShaderId = id;
                    }
                }
            }

            // ── Shader description / metadata ─────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.preferredWidth: 0 // Don't expand parent based on content
                Layout.preferredHeight: Kirigami.Units.gridUnit * 4
                Layout.minimumHeight: Kirigami.Units.gridUnit * 4
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: root.hasShaderEffect
                spacing: Kirigami.Units.smallSpacing

                Label {
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
                    // One binding: a font.<sub> sibling next to a whole-group
                    // `font:` is an illegal duplicate binding that fails the
                    // whole document. smallFontWith passes only the size
                    // dimension the theme font actually carries.
                    font: root.smallFontWith({
                        italic: !(root.currentShaderInfo && root.currentShaderInfo.description)
                    })
                    verticalAlignment: Text.AlignTop
                }

                Label {
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
                    // One binding, one valid size (see the description label above).
                    font: root.smallFontWith({
                        italic: true
                    })
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
                visible: root.hasShaderEffect && root.shaderParams.length > 0
            }

            // ── Parameters editor (toolbar + flat/grouped rows) ───────
            PZCommon.ParameterEditor {
                id: paramEditor

                Layout.fillWidth: true
                visible: root.hasShaderEffect
                parameters: root.shaderParams
                currentValues: root.pendingParams
                lockedParams: root.lockedParams
                enableLocking: true
                enableRandomize: true
                // This dialog has its own "Defaults" button in the footer
                // button row, so suppress the editor's header reset to avoid a
                // duplicate (same pattern as ShaderBrowserDetailDialog).
                enableReset: false
                enableGroups: true
                enableImage: true
                onValueChanged: function (id, value) {
                    root.setPendingParam(id, value);
                }
                onLockToggled: function (id, locked) {
                    root.setParamLocked(id, locked);
                }
                onLockAllRequested: function (lock) {
                    root.toggleAllLocks(lock);
                }
                onRandomizeRequested: root.randomizeParameters()
                onRequestColorPicker: function (id, name, current) {
                    root.openColorDialog(id, name, current);
                }
                onRequestImagePicker: function (id) {
                    root.openImageDialog(id);
                }

                // Editor-specific extras: metadata-driven preset menu.
                toolbarTrailing: Component {
                    ToolButton {
                        id: metadataPresetButton

                        icon.name: "bookmarks"
                        display: ToolButton.IconOnly
                        visible: {
                            var info = root.currentShaderInfo;
                            return info && info.presets && info.presets.length > 0;
                        }
                        ToolTip.text: i18nc("@info:tooltip", "Apply a built-in preset")
                        Accessible.name: i18nc("@action:button", "Presets")
                        Accessible.description: ToolTip.text
                        onClicked: metadataPresetMenu.open()

                        Menu {
                            id: metadataPresetMenu

                            // Same Qt 6 menu-lifecycle hardening as
                            // CategoryMenuButton: opaque palette + empty
                            // transitions keep finalizeExitTransition
                            // synchronous and avoid the QQmlData::destroyed
                            // race that fires when MenuItem-based exit
                            // animations run while the parent dialog is
                            // tearing down.
                            palette.window: Kirigami.Theme.backgroundColor
                            y: metadataPresetButton.height

                            Instantiator {
                                model: {
                                    var info = root.currentShaderInfo;
                                    return (info && info.presets) ? info.presets : [];
                                }
                                onObjectAdded: function (index, object) {
                                    metadataPresetMenu.insertItem(index, object);
                                }
                                onObjectRemoved: function (index, object) {
                                    metadataPresetMenu.removeItem(object);
                                }

                                // ItemDelegate (not MenuItem) so Qt's
                                // onItemTriggered → dismiss() cascade
                                // never starts; the menu hides via the
                                // explicit Qt.callLater below, after the
                                // click handler has returned.
                                delegate: ItemDelegate {
                                    required property var modelData

                                    text: modelData.name
                                    Accessible.name: modelData.name
                                    onClicked: {
                                        // Snapshot BOTH the preset name and the
                                        // shader id at click time. Without the id
                                        // snapshot, a registry refresh between
                                        // click and the deferred apply (e.g.
                                        // `availableShaders` reload) could shift
                                        // `root.pendingShaderId` out from under
                                        // us, applying the preset to the wrong
                                        // shader.
                                        var presetName = modelData.name;
                                        var shaderIdAtClick = root.pendingShaderId;
                                        Qt.callLater(function () {
                                            metadataPresetMenu.visible = false;
                                            if (!editorController)
                                                return;

                                            // Bail if the shader changed between click
                                            // and apply — better a no-op than a wrong-shader apply.
                                            if (root.pendingShaderId !== shaderIdAtClick)
                                                return;

                                            var preset = editorController.presetParams(shaderIdAtClick, presetName);
                                            if (preset && Object.keys(preset).length > 0)
                                                root.pendingParams = preset;
                                        });
                                    }
                                }
                            }

                            enter: Transition {}

                            exit: Transition {}
                        }
                    }
                }
            }

            // ── Disabled state message ────────────────────────────────
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

            // ── Preset error inline ───────────────────────────────────
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

            // ── Footer (Load/Save preset, Defaults, Apply) ────────────
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

        // ═══════════════════════════════════════════════════════════════
        // RIGHT: Live shader preview (hidden when effect disabled - no space)
        // ═══════════════════════════════════════════════════════════════
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

                readonly property var cfg: root.previewShaderConfig || ({})
                // Cached so the per-frame (iTime) config rebuild doesn't read the
                // audio spectrum off editorController every frame (a QVariant
                // vector copy) and doesn't hand the renderer a fresh container
                // reference each tick; updates only on audioSpectrumChanged.
                readonly property var previewAudioSpectrum: editorController ? editorController.audioSpectrum : []
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

                // Preview well resolves against the View color set
                Kirigami.Theme.colorSet: Kirigami.Theme.View
                Kirigami.Theme.inherit: false
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                color: Kirigami.Theme.backgroundColor
                radius: Kirigami.Units.smallSpacing
                clip: true
                onWidthChanged: debouncePreviewUpdate.restart()
                onHeightChanged: debouncePreviewUpdate.restart()

                HoverHandler {
                    id: previewHover

                    onPointChanged: {
                        if (previewHover.hovered)
                            previewBackground.previewMouse = Qt.point(previewHover.point.position.x, previewHover.point.position.y);
                    }
                    onHoveredChanged: {
                        if (!previewHover.hovered)
                            previewBackground.previewMouse = Qt.point(-1, -1);
                    }
                }

                // Shader preview via Loader — destroy and recreate the ZoneShaderItem
                // on each shader switch so all RHI resources (FBOs, pipelines, textures)
                // are fully released before new ones are created. In-place resource
                // swapping triggers heap corruption in NVIDIA's EGL driver (595.x).
                Loader {
                    id: shaderPreviewLoader

                    anchors.fill: parent
                    active: root.previewShaderConfig !== null
                    visible: item === null || item.status !== ZoneShaderItem.Error

                    // Shared zone-shader renderer (org.plasmazones.common) — the
                    // single source of truth for the ZoneShaderItem bindings, also
                    // used by the settings-app preview + the overlay. Kept inside
                    // this Loader so a shader switch still destroys/recreates the
                    // render node (NVIDIA EGL 595.x heap-corruption workaround —
                    // see hideShaderPreview()). The whole feed (incl. label /
                    // wallpaper textures) rides the config object.
                    sourceComponent: PZCommon.ZoneShaderRenderer {
                        // cfg is `previewShaderConfig || ({})`, so it is never
                        // null — read it directly; per-key `|| default` covers the
                        // empty-{} (no-shader) case.
                        config: ({
                                "shaderSource": previewBackground.cfg.shaderUrl || "",
                                "paramPreamble": previewBackground.cfg.paramPreamble || "",
                                "bufferShaderPaths": previewBackground.cfg.bufferShaderPaths || [],
                                "bufferFeedback": previewBackground.cfg.bufferFeedback || false,
                                "bufferScale": previewBackground.cfg.bufferScale !== undefined ? previewBackground.cfg.bufferScale : 1,
                                "bufferWrap": previewBackground.cfg.bufferWrap || "clamp",
                                "bufferWraps": previewBackground.cfg.bufferWraps || [],
                                "bufferFilter": previewBackground.cfg.bufferFilter || "linear",
                                "bufferFilters": previewBackground.cfg.bufferFilters || [],
                                "useDepthBuffer": previewBackground.cfg.useDepthBuffer || false,
                                "useWallpaper": previewBackground.cfg.useWallpaper || false,
                                "zones": previewBackground.cfg.zones || [],
                                "shaderParams": previewBackground.cfg.shaderParams || ({}),
                                "labelsTexture": previewBackground.cfg.labelsTexture,
                                "wallpaperTexture": previewBackground.cfg.wallpaperTexture,
                                "hoveredZoneIndex": previewBackground.previewHoveredZone,
                                "iTime": root.previewITime,
                                "iTimeDelta": root.previewTimeDelta,
                                "iFrame": root.previewFrame,
                                "iMouse": previewBackground.previewMouse,
                                "audioSpectrum": previewBackground.previewAudioSpectrum
                            })
                    }
                }

                // T3.2: shared in-app compile-error banner (also used by the
                // settings-app shader browser).
                PZCommon.ShaderCompileErrorBanner {
                    anchors.centerIn: parent
                    width: Math.min(parent.width - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 22)
                    height: Math.min(parent.height - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 12)
                    errorLog: (shaderPreviewLoader.item && shaderPreviewLoader.item.status === ZoneShaderItem.Error) ? ((shaderPreviewLoader.item.errorLog && shaderPreviewLoader.item.errorLog.length > 0) ? shaderPreviewLoader.item.errorLog : i18nc("@info shader preview", "No error details available.")) : ""
                }

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

        options: ColorDialog.ShowAlphaChannel

        property string paramId: ""
        property string paramName: ""

        title: i18nc("@title:window", "Choose %1", paramName)
        onAccepted: {
            if (paramId)
                root.setPendingParam(paramId, selectedColor.toString());
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    // SHARED IMAGE FILE DIALOG (owned by dialog root, not by delegate row)
    // ═══════════════════════════════════════════════════════════════════════
    FileDialog {
        id: shaderImageDialog

        property string paramId: ""

        title: i18nc("@title:window", "Choose Image")
        nameFilters: [i18nc("@item:inlistbox", "Image files (*.png *.jpg *.jpeg *.bmp *.webp *.svg *.svgz)"), i18nc("@item:inlistbox", "All files (*)")]
        fileMode: FileDialog.OpenFile
        onAccepted: {
            if (paramId)
                root.setPendingParam(paramId, filePathFromUrl(selectedFile));

            root.restoreShaderPreview();
        }
        onRejected: {
            root.restoreShaderPreview();
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
                return;

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
            var ok = editorController.saveShaderPreset(path, root.pendingShaderId, root.pendingParams || {}, presetName);
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
                return;

            var path = filePathFromUrl(selectedFile);
            var result = editorController.loadShaderPreset(path);
            if (result && result.shaderId) {
                root.pendingShaderId = result.shaderId;
                root.pendingParams = result.shaderParams || {};
                root.presetErrorMessage = "";
            }
        }
    }
}
