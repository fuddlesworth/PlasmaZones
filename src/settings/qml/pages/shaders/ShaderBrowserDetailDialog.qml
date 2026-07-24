// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami
import PlasmaZones
import org.plasmazones.common as PZCommon

import "../../js/FontUtils.js" as FontUtils

/**
 * @brief Detail view for a single shader effect (pack-agnostic).
 *
 * Side-by-side layout (mirroring the zone editor): the metadata + parameters
 * scroll independently on the left, and a PINNED preview fills the right — so
 * the preview never scrolls out of view and the parameters always have room.
 * Drives both the animation-shaders browser and the snapping-overlay-shaders
 * browser via a `bridge`.
 *
 * When the bridge also exposes a `previewController` (zone/overlay browser
 * only — see SnappingShadersPageController), the right pane is a LIVE
 * ZoneShaderItem preview and the left column an editable
 * ParameterEditor whose changes are transient (never persisted). The
 * animation browser has no previewController, so it shows no preview pane —
 * just a read-only parameter list.
 *
 * Required:
 *   - `effect`: var — set by the host before calling `open()`.
 *   - `bridge`: QtObject — `shaderEffectUsages(id)`; optionally `previewController`.
 * Optional:
 *   - `usagesRev`: int — invalidates the cached `shaderEffectUsages` result.
 *   - `usageHeaderTextFn`: function(count) → string — domain-tuned copy.
 */
Kirigami.Dialog {
    id: root

    property var effect: null
    required property var bridge
    property int usagesRev: 0
    property var usageHeaderTextFn: function (count) {
        return i18ncp("@info shader usage section header", "Used in %n event", "Used in %n events", count);
    }
    readonly property string _description: effect && typeof effect.description === "string" ? effect.description : ""
    readonly property string _author: effect && effect.author && effect.author.length > 0 ? effect.author : ""
    readonly property string _version: effect && effect.version && effect.version.length > 0 ? effect.version : ""
    readonly property var _usages: {
        usagesRev; // reactive dep
        var id = effect ? effect.id : "";
        if (!id || id.length === 0 || !bridge)
            return [];

        return bridge.shaderEffectUsages(id);
    }
    readonly property bool _hasParameters: effect && effect.parameters && effect.parameters.length > 0

    // Percent-encode a local file path for use in a file:// URL. `encodeURI`
    // handles spaces and unicode while preserving path separators, but leaves
    // `#` and `?` untouched, so those two are escaped explicitly or they would
    // be parsed as fragment/query delimiters.
    // Twin site: ShaderBrowserCard.qml preview Image source.
    function _encodeFilePath(path) {
        return encodeURI(path).replace(/#/g, "%23").replace(/\?/g, "%3F");
    }

    // ── T3.1 live preview (zone/overlay browser only) ──────────────────
    // The zone-shader bridge exposes a shared ShaderPreviewController; the
    // animation bridge does not. So the right pane is a live render + the left
    // column an editable editor only here.
    readonly property var previewController: bridge && bridge.previewController ? bridge.previewController : null
    readonly property bool _livePreview: previewController !== null && effect !== null
    // Transient (non-persisted) state driving the preview.
    property var _liveParams: ({})
    property var _lockedParams: ({})
    property var _shaderInfo: ({})
    property var _translatedParams: ({})
    property string _presetError: ""
    // T3.2: drives the preview-renderer Loader. Toggled off→on in _resetPreview
    // so the ZoneShaderItem is destroyed and recreated on every shader switch —
    // a fresh item has no inherited Error/errorLog, so the placeholder covers the
    // load and the compile-error banner can only reflect the shader in view.
    property bool _rendererActive: false
    // Animated clock for the preview shader.
    property real _previewITime: 0
    property real _previewTimeDelta: 0
    property real _previewLastTime: 0
    property int _previewFrame: 0

    function _resetPreview() {
        if (!_livePreview)
            return;
        _shaderInfo = previewController.getShaderInfo(effect.id) || ({});
        var p = {};
        var params = effect.parameters || [];
        for (var i = 0; i < params.length; i++) {
            if (params[i] && params[i].id !== undefined && params[i].default !== undefined)
                p[params[i].id] = params[i].default;
        }
        _liveParams = p;
        _lockedParams = {};
        _recompute();
        // Destroy + recreate the renderer so it starts fresh on the new shader
        // (no previous shader's stale Error). Deactivate now, reactivate next
        // tick — _shaderInfo is already set above, so the new item loads it.
        _rendererActive = false;
        Qt.callLater(function () {
            // Guard on `opened` too: if the dialog was closed between the
            // deferral and this call, don't re-arm the renderer.
            root._rendererActive = root.opened && root._livePreview;
        });
    }
    function _recompute() {
        if (!_livePreview)
            return;
        _translatedParams = previewController.translateShaderParams(effect.id, _liveParams) || ({});
    }
    function _setLiveParam(id, value) {
        var next = Object.assign({}, _liveParams);
        next[id] = value;
        _liveParams = next;
        _recompute();
    }

    onOpened: {
        // The dialog instance is reused per shader, so clear any preset error
        // left over from the previous shader's session before showing this one.
        _presetError = "";
        if (_livePreview) {
            _resetPreview();
            previewController.startAudioCapture();
        }
    }
    onClosed: {
        // Tear the renderer down on close so its (possibly Error) state can't
        // survive on the reused dialog and flash on the next shader's open —
        // the next _resetPreview rebuilds it fresh.
        _rendererActive = false;
        if (previewController)
            previewController.stopAudioCapture();
    }

    function _fmt(v) {
        if (typeof v === "number")
            return Number.isInteger(v) ? String(v) : v.toFixed(2);

        if (typeof v === "boolean")
            return v ? i18nc("@info bool true", "Yes") : i18nc("@info bool false", "No");

        return String(v);
    }

    title: effect ? (effect.name || effect.id || "") : ""
    // Wide (side-by-side with the live preview) for the zone/overlay browser;
    // narrower single-column for the animation browser, which has no preview.
    preferredWidth: _livePreview ? Kirigami.Units.gridUnit * 54 : Kirigami.Units.gridUnit * 38
    maximumHeight: Kirigami.Units.gridUnit * 26
    standardButtons: Kirigami.Dialog.Close
    padding: Kirigami.Units.largeSpacing

    // Preset controls on the footer's left (Close stays on the right) — live
    // preview only. Order: Load · Save · gap · Default.
    footerLeadingComponent: Component {
        // Span the full params column (left ScrollView) so Load · Save sit at
        // its left and Default anchors to its right edge — lining up under the
        // params/preview split and the per-row lock column above.
        Item {
            visible: root._livePreview && root._hasParameters
            // Width = the params content width: availableWidth already excludes
            // the scrollbar, and subtracting one largeSpacing matches the params
            // column's own right margin, so Default's right edge lines up with the
            // per-row lock column rather than the divider. MUST be 0 when hidden:
            // the animation dialog has no preview column, so availableWidth is
            // nearly the full dialog width — claiming that here would make the
            // footer wider than the dialog and feed back into availableWidth, a
            // runaway layout loop that freezes the dialog.
            implicitWidth: visible ? detailsScroll.availableWidth - Kirigami.Units.largeSpacing : 0
            implicitHeight: visible ? presetRow.implicitHeight : 0

            RowLayout {
                id: presetRow

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Kirigami.Units.smallSpacing

                Button {
                    text: i18nc("@action:button", "Load Preset…")
                    icon.name: "document-open"
                    Accessible.name: text
                    onClicked: {
                        // See root._encodeFilePath for the encoding rationale.
                        shaderPresetLoadDialog.currentFolder = Qt.resolvedUrl("file://" + root._encodeFilePath(root.previewController.shaderPresetDirectory()));
                        shaderPresetLoadDialog.open();
                    }
                }

                Button {
                    text: i18nc("@action:button", "Save Preset…")
                    icon.name: "document-save"
                    Accessible.name: text
                    onClicked: {
                        // See root._encodeFilePath for the encoding rationale.
                        shaderPresetSaveDialog.currentFolder = Qt.resolvedUrl("file://" + root._encodeFilePath(root.previewController.shaderPresetDirectory()));
                        shaderPresetSaveDialog.open();
                    }
                }
            }

            Button {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: i18nc("@action:button reset shader parameters", "Default")
                icon.name: "edit-reset"
                Accessible.name: text
                onClicked: root._resetPreview()
            }
        }
    }

    Item {
        implicitWidth: root.preferredWidth - root.leftPadding - root.rightPadding
        implicitHeight: mainRow.implicitHeight

        RowLayout {
            id: mainRow

            anchors.left: parent.left
            anchors.right: parent.right
            spacing: Kirigami.Units.largeSpacing

            // ── LEFT: scrollable metadata + parameters ──────────────────
            ScrollView {
                id: detailsScroll

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: Kirigami.Units.gridUnit * 18
                Layout.maximumHeight: Kirigami.Units.gridUnit * 22
                contentWidth: availableWidth
                clip: true

                ColumnLayout {
                    // Leave a right margin so content (the param-count label, the
                    // per-row value + lock) doesn't butt against the divider /
                    // scrollbar.
                    width: detailsScroll.availableWidth - Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.largeSpacing

                    // ── Header row: category pill + badges + param count ──
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing

                        Rectangle {
                            visible: root.effect && root.effect.category && root.effect.category.length > 0
                            radius: height / 2
                            // Filled highlight chip with contrasting text so it's
                            // legible on the dark dialog (the faint-tint version
                            // was nearly invisible).
                            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.85)
                            Layout.preferredWidth: categoryLabel.implicitWidth + Kirigami.Units.largeSpacing
                            Layout.preferredHeight: categoryLabel.implicitHeight + Kirigami.Units.smallSpacing
                            Layout.alignment: Qt.AlignVCenter

                            Label {
                                id: categoryLabel

                                anchors.centerIn: parent
                                text: root.effect ? (root.effect.category || "") : ""
                                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                                font.weight: Font.Medium
                                color: Kirigami.Theme.highlightedTextColor
                            }
                        }

                        Label {
                            visible: root.effect && root.effect.isUserEffect
                            text: i18nc("@info shader source badge", "User")
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.highlightColor
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Label {
                            visible: root._hasParameters
                            text: i18np("%n parameter", "%n parameters", root.effect ? (root.effect.parameters || []).length : 0)
                            font: Kirigami.Theme.smallFont
                            color: Kirigami.Theme.disabledTextColor
                        }
                    }

                    // ── Description + author/version ──────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root._description.length > 0 || root._author.length > 0 || root._version.length > 0
                        spacing: Kirigami.Units.smallSpacing

                        Label {
                            visible: root._description.length > 0
                            Layout.fillWidth: true
                            text: root._description
                            wrapMode: Text.WordWrap
                        }

                        Label {
                            visible: root._author.length > 0 || root._version.length > 0
                            Layout.fillWidth: true
                            text: {
                                var parts = [];
                                if (root._author.length > 0)
                                    parts.push(i18nc("@info shader author", "by %1", root._author));

                                if (root._version.length > 0)
                                    parts.push(i18nc("@info shader version", "v%1", root._version));

                                return parts.join(" · ");
                            }
                            color: Kirigami.Theme.disabledTextColor
                            // One binding: a font.<sub> sibling next to a whole-group `font:` is an
                            // illegal duplicate binding that fails the whole document. FontUtils
                            // passes only the size dimension the theme font actually carries.
                            font: FontUtils.withProps(Kirigami.Theme.smallFont, {
                                italic: true
                            })
                            elide: Text.ElideRight
                        }
                    }

                    // ── Used-in section ──────────────────────────────────
                    ColumnLayout {
                        Layout.fillWidth: true
                        visible: root._usages.length > 0
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Separator {
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Kirigami.Units.smallSpacing

                            Kirigami.Icon {
                                source: "checkmark"
                                implicitWidth: Kirigami.Units.iconSizes.small
                                implicitHeight: Kirigami.Units.iconSizes.small
                                color: Kirigami.Theme.positiveTextColor
                            }

                            Label {
                                text: root.usageHeaderTextFn(root._usages.length)
                                font.weight: Font.DemiBold
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                            text: {
                                var labels = [];
                                for (var i = 0; i < root._usages.length; i++)
                                    labels.push(root._usages[i].label || root._usages[i].path);
                                return labels.join(", ");
                            }
                            color: Kirigami.Theme.disabledTextColor
                            font: Kirigami.Theme.smallFont
                            wrapMode: Text.WordWrap
                        }
                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                        visible: root._hasParameters
                    }

                    Kirigami.InlineMessage {
                        id: presetErrorMessage

                        Layout.fillWidth: true
                        // Visibility is driven imperatively in one direction
                        // only: the Connections below shows the message when a
                        // new error lands, and the close button hides it. A
                        // declarative `visible: _presetError.length > 0`
                        // binding would be severed the first time the close
                        // button imperatively wrote visible = false, so later
                        // preset errors would never show again.
                        visible: false
                        type: Kirigami.MessageType.Error
                        text: root._presetError
                        showCloseButton: true
                        onVisibleChanged: if (!visible)
                            root._presetError = ""

                        Connections {
                            target: root
                            function on_PresetErrorChanged() {
                                presetErrorMessage.visible = root._presetError.length > 0;
                            }
                        }
                    }

                    // ── Parameters ────────────────────────────────────────
                    // Editable editor (live preview) carries its own toolbar +
                    // "Parameters" header, so the read-only heading below is
                    // shown only for the animation browser.
                    Kirigami.Heading {
                        visible: root._hasParameters && !root._livePreview
                        text: i18nc("@title:group shader parameters section", "Parameters")
                        level: 4
                    }

                    // Editable editor — live (zone/overlay) browser only. Wrapped
                    // in a Loader so it is NOT instantiated for the animation
                    // browser (which uses the read-only Repeater below); otherwise
                    // it eagerly builds slider rows for params it never shows.
                    Loader {
                        Layout.fillWidth: true
                        active: root._livePreview && root._hasParameters

                        sourceComponent: PZCommon.ParameterEditor {
                            id: paramEditor

                            compact: false
                            enableLocking: true
                            enableRandomize: true
                            // This dialog has its own "Default" reset button in
                            // its footer, so suppress the editor's header reset
                            // to avoid a duplicate affordance.
                            enableReset: false
                            enableGroups: true
                            enableImage: true
                            parameters: root.effect && root.effect.parameters ? root.effect.parameters : []
                            currentValues: root._liveParams
                            lockedParams: root._lockedParams
                            onValueChanged: function (id, value) {
                                root._setLiveParam(id, value);
                            }
                            onLockToggled: function (id, locked) {
                                root._lockedParams = paramEditor.lockedAfterToggle(id, locked);
                            }
                            onLockAllRequested: function (locked) {
                                root._lockedParams = paramEditor.lockedAfterAllToggle(locked);
                            }
                            onRandomizeRequested: {
                                root._liveParams = paramEditor.computeRandomized();
                                root._recompute();
                            }
                            onRequestColorPicker: function (id, name, current) {
                                shaderColorDialog.openFor(id, current);
                            }
                            onRequestImagePicker: function (id) {
                                shaderImageDialog.paramId = id;
                                shaderImageDialog.open();
                            }
                        }
                    }

                    // Read-only fallback for the animation browser.
                    Repeater {
                        model: !root._livePreview && root.effect && root.effect.parameters ? root.effect.parameters : []

                        delegate: Rectangle {
                            id: paramRow

                            required property var modelData
                            required property int index

                            Layout.fillWidth: true
                            implicitHeight: rowContent.implicitHeight + Kirigami.Units.smallSpacing
                            radius: Kirigami.Units.smallSpacing / 2
                            color: index % 2 === 0 ? "transparent" : Kirigami.Theme.alternateBackgroundColor

                            RowLayout {
                                id: rowContent

                                anchors.fill: parent
                                anchors.leftMargin: Kirigami.Units.smallSpacing
                                anchors.rightMargin: Kirigami.Units.smallSpacing
                                spacing: Kirigami.Units.smallSpacing

                                Label {
                                    Layout.preferredWidth: Kirigami.Units.gridUnit * 10
                                    Layout.alignment: Qt.AlignVCenter
                                    text: paramRow.modelData ? (paramRow.modelData.name || paramRow.modelData.id || "") : ""
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                }

                                Label {
                                    Layout.fillWidth: true
                                    Layout.alignment: Qt.AlignVCenter
                                    text: {
                                        var p = paramRow.modelData;
                                        if (!p)
                                            return "";

                                        var parts = [];
                                        if (p.type && p.type.length > 0)
                                            parts.push(p.type);

                                        if (p.min !== undefined && p.max !== undefined)
                                            parts.push(i18nc("@info parameter range", "[%1 .. %2]", root._fmt(p.min), root._fmt(p.max)));

                                        if (p.default !== undefined)
                                            parts.push(i18nc("@info parameter default value", "default %1", root._fmt(p.default)));

                                        return parts.join(" · ");
                                    }
                                    color: Kirigami.Theme.disabledTextColor
                                    font: Kirigami.Theme.smallFont
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
            }

            // ── RIGHT: pinned preview (fills the column) ────────────────
            // Only the zone/overlay browser has a live preview — hidden (and so
            // excluded from the row, letting the params fill width) for the
            // animation browser.
            Item {
                visible: root._livePreview
                Layout.preferredWidth: Kirigami.Units.gridUnit * 24
                Layout.minimumWidth: Kirigami.Units.gridUnit * 20
                Layout.fillHeight: true

                // Live ZoneShaderItem preview (zone/overlay browser).
                Rectangle {
                    id: livePreviewPane

                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    visible: root._livePreview
                    radius: Kirigami.Units.smallSpacing
                    // Intentionally a true-black backdrop (not a theme color): the
                    // shader renders over this, and a tinted background would
                    // contaminate the previewed colors.
                    color: "black"
                    border.width: 1
                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                    clip: true

                    // Zones the preview renders over — shared by the renderer,
                    // the label texture, and the hover hit-test. Recomputed on
                    // resize; the settings backend supplies a 2-zone sample so
                    // multi-zone + per-zone-highlight effects are visible.
                    readonly property var _zones: (root._livePreview && root.previewController) ? root.previewController.zonesForShaderPreview(Math.max(1, Math.round(width)), Math.max(1, Math.round(height))) : []
                    // Mouse position within the pane (preview pixels); -1,-1 when
                    // not hovering. Drives iMouse + the hovered-zone highlight.
                    property point _mouse: Qt.point(-1, -1)
                    readonly property int _hoveredZone: {
                        if (_mouse.x < 0 || _mouse.y < 0)
                            return -1;
                        for (var i = 0; i < _zones.length; i++) {
                            var z = _zones[i];
                            var zx = (z.x !== undefined) ? z.x : 0;
                            var zy = (z.y !== undefined) ? z.y : 0;
                            var zw = (z.width !== undefined) ? z.width : 0;
                            var zh = (z.height !== undefined) ? z.height : 0;
                            if (_mouse.x >= zx && _mouse.x < zx + zw && _mouse.y >= zy && _mouse.y < zy + zh)
                                return i;
                        }
                        return -1;
                    }

                    HoverHandler {
                        id: previewHover

                        onPointChanged: {
                            if (previewHover.hovered)
                                livePreviewPane._mouse = Qt.point(previewHover.point.position.x, previewHover.point.position.y);
                        }
                        onHoveredChanged: {
                            if (!previewHover.hovered)
                                livePreviewPane._mouse = Qt.point(-1, -1);
                        }
                    }

                    // ~60fps clock — only ticks while the pane is shown.
                    Timer {
                        running: livePreviewPane.visible
                        interval: 16
                        repeat: true
                        onTriggered: {
                            var now = Date.now() / 1000;
                            var delta = Math.min(now - root._previewLastTime, 0.1);
                            root._previewLastTime = now;
                            root._previewITime += delta;
                            root._previewTimeDelta = delta;
                            root._previewFrame = (root._previewFrame + 1) % 1000000000;
                        }
                    }

                    // Expensive feeds cached as properties so the per-frame
                    // config rebuild below (iTime) doesn't re-run C++ calls —
                    // these recompute only when zones / size / shader change.
                    readonly property string _preamble: (root._livePreview && root.previewController) ? root.previewController.shaderParamPreamble(root.effect.id) : ""
                    readonly property var _labelsTex: (root._livePreview && root.previewController && _zones.length > 0) ? root.previewController.buildLabelsTexture(_zones, Math.max(1, Math.round(width)), Math.max(1, Math.round(height))) : null
                    readonly property var _wallpaperTex: (root._livePreview && root.previewController && root._shaderInfo.wallpaper === true) ? root.previewController.loadWallpaperTexture() : null
                    // Cached so the per-frame config rebuild below doesn't read it
                    // off the controller every frame (a QVariant vector copy) and
                    // doesn't hand the renderer a new container reference each tick;
                    // updates only on audioSpectrumChanged.
                    readonly property var _audioSpectrum: root.previewController ? root.previewController.audioSpectrum : []

                    // Shared zone-shader renderer (org.plasmazones.common) — the
                    // single source of truth for the ZoneShaderItem bindings (also
                    // used by the overlay). Wrapped in a Loader and recreated on
                    // every shader switch (via _rendererActive, toggled in
                    // _resetPreview): a fresh item starts at Loading with no
                    // inherited Error/errorLog, so the placeholder shows during the
                    // load and the banner only ever reflects THIS shader.
                    Loader {
                        id: rendererLoader

                        anchors.fill: parent
                        active: root._rendererActive
                        // Reveal the surface only once it has actually compiled —
                        // the placeholder covers loading, the banner covers errors.
                        visible: item !== null && item.status === ZoneShaderItem.Ready

                        sourceComponent: PZCommon.ZoneShaderRenderer {
                            config: ({
                                    "shaderSource": root._shaderInfo.shaderUrl || "",
                                    "paramPreamble": livePreviewPane._preamble,
                                    "bufferShaderPaths": root._shaderInfo.bufferShaderPaths || [],
                                    "bufferFeedback": root._shaderInfo.bufferFeedback || false,
                                    "bufferScale": root._shaderInfo.bufferScale !== undefined ? root._shaderInfo.bufferScale : 1,
                                    "bufferWrap": root._shaderInfo.bufferWrap || "clamp",
                                    "bufferWraps": root._shaderInfo.bufferWraps || [],
                                    "bufferFilter": root._shaderInfo.bufferFilter || "linear",
                                    "bufferFilters": root._shaderInfo.bufferFilters || [],
                                    "useDepthBuffer": root._shaderInfo.depthBuffer || false,
                                    "useWallpaper": root._shaderInfo.wallpaper || false,
                                    "zones": livePreviewPane._zones,
                                    "shaderParams": root._translatedParams,
                                    "hoveredZoneIndex": livePreviewPane._hoveredZone,
                                    "iTime": root._previewITime,
                                    "iTimeDelta": root._previewTimeDelta,
                                    "iFrame": root._previewFrame,
                                    "iMouse": livePreviewPane._mouse,
                                    "audioSpectrum": livePreviewPane._audioSpectrum,
                                    "labelsTexture": livePreviewPane._labelsTex,
                                    "wallpaperTexture": livePreviewPane._wallpaperTex
                                })
                        }
                    }

                    // Neutral placeholder while the freshly-created renderer is
                    // still loading / compiling the new shader (the "round trip").
                    Label {
                        anchors.centerIn: parent
                        width: parent.width - Kirigami.Units.largeSpacing * 2
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Kirigami.Theme.disabledTextColor
                        text: i18nc("@info:placeholder shader preview", "Preview unavailable")
                        // Shown until the new shader is Ready (or has errored, when
                        // the banner takes over) — also covers the recreate tick
                        // when the Loader item is briefly null. The enclosing pane
                        // is hidden for the animation browser, so no extra gate.
                        visible: !rendererLoader.item || (rendererLoader.item.status !== ZoneShaderItem.Ready && rendererLoader.item.status !== ZoneShaderItem.Error)
                    }

                    // T3.2: surface the live preview's actual GLSL compile error
                    // in-app (shared with the editor preview). The renderer is
                    // recreated per shader, so its Error is always THIS shader's.
                    PZCommon.ShaderCompileErrorBanner {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 22)
                        height: Math.min(parent.height - Kirigami.Units.largeSpacing * 2, Kirigami.Units.gridUnit * 12)
                        errorLog: (rendererLoader.item && rendererLoader.item.status === ZoneShaderItem.Error) ? ((rendererLoader.item.errorLog && rendererLoader.item.errorLog.length > 0) ? rendererLoader.item.errorLog : i18nc("@info shader preview", "No error details available.")) : ""
                    }
                }
            }
        }
    }

    // Color picker for color params (child of the dialog root, mirroring the
    // editor's ShaderSettingsDialog). Outlives any param row destroyed mid-edit.
    ColorDialog {
        id: shaderColorDialog

        options: ColorDialog.ShowAlphaChannel

        property string paramId: ""

        function openFor(id, current) {
            paramId = id;
            if (current !== undefined && current !== null)
                selectedColor = current;
            open();
        }

        onAccepted: {
            if (paramId.length > 0)
                root._setLiveParam(paramId, selectedColor);
        }
    }

    // Image picker for image (texture) params.
    FileDialog {
        id: shaderImageDialog

        property string paramId: ""

        title: i18nc("@title:window", "Select Image")
        fileMode: FileDialog.OpenFile
        nameFilters: [i18nc("@item:inlistbox image file filter", "Images (*.png *.jpg *.jpeg *.bmp *.webp)")]

        onAccepted: {
            if (paramId.length > 0)
                root._setLiveParam(paramId, settingsController.urlToLocalFile(selectedFile));
        }
    }

    // Surface preset save/load failures from the shared controller.
    Connections {
        target: root.previewController
        enabled: root.previewController !== null
        function onShaderPresetSaveFailed(error) {
            root._presetError = error;
        }
        function onShaderPresetLoadFailed(error) {
            root._presetError = error;
        }
    }

    FileDialog {
        id: shaderPresetSaveDialog

        title: i18nc("@title:window", "Save Shader Preset")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [i18nc("@item:inlistbox preset file filter", "Shader presets (*.json)")]

        onAccepted: {
            if (root.previewController && root.effect)
                root.previewController.saveShaderPreset(settingsController.urlToLocalFile(selectedFile), root.effect.id, root._liveParams, "");
        }
    }

    FileDialog {
        id: shaderPresetLoadDialog

        title: i18nc("@title:window", "Load Shader Preset")
        fileMode: FileDialog.OpenFile
        nameFilters: [i18nc("@item:inlistbox preset file filter", "Shader presets (*.json)")]

        onAccepted: {
            if (!root.previewController || !root.effect)
                return;
            var r = root.previewController.loadShaderPreset(settingsController.urlToLocalFile(selectedFile));
            if (!r || !r.shaderParams)
                return;
            // This detail dialog is bound to a single shader (root.effect) and
            // cannot switch shaders, so a preset saved for a different shader
            // would silently apply mismatched params. Reject it loudly instead.
            if (r.shaderId && r.shaderId !== root.effect.id) {
                root._presetError = i18nc("@info", "This preset was saved for a different shader.");
                return;
            }
            // The load succeeded — drop any error left from an earlier
            // failed save/load so the stale message doesn't sit next to a
            // freshly applied preset.
            root._presetError = "";
            // Apply the preset's values onto the current shader's parameter set
            // (preset value where present, else the param default), so a preset
            // saved for a slightly different param list still loads cleanly.
            var next = {};
            var params = root.effect.parameters || [];
            for (var i = 0; i < params.length; i++) {
                var p = params[i];
                if (!p || p.id === undefined)
                    continue;
                next[p.id] = (r.shaderParams[p.id] !== undefined) ? r.shaderParams[p.id] : (p.default !== undefined ? p.default : root._liveParams[p.id]);
            }
            // Carry SVG image params' `<id>_svgSize` companion (not a schema
            // param, so the loop above skips it): preset value where present,
            // else the current size. Without this, loading a preset would
            // revert a custom SVG render size to the default, unlike the editor
            // dialog and the randomize path which preserve it.
            for (var j = 0; j < params.length; j++) {
                var ip = params[j];
                if (!ip || ip.id === undefined || ip.type !== "image")
                    continue;
                var svgKey = ip.id + "_svgSize";
                if (r.shaderParams[svgKey] !== undefined)
                    next[svgKey] = r.shaderParams[svgKey];
                else if (root._liveParams[svgKey] !== undefined)
                    next[svgKey] = root._liveParams[svgKey];
            }
            root._liveParams = next;
            root._recompute();
        }
    }
}
