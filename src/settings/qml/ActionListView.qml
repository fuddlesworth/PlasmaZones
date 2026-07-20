// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.settings

import "FontUtils.js" as FontUtils

/**
 * @brief Read-only display of a rule's actions list — the "THEN" half of
 *        the rule preview.
 *
 * Mirrors `MatchExpressionView` structurally for visual consistency: each
 * action renders at depth-1 in a flat tree, with a vertical guide and
 * L-stub at column 0 plus a blue bullet at the row's content edge. The
 * row carries a bold action-type label, then a sequence of
 * `LABEL value-pill` pairs (one per parameter), all aligned in tabular
 * columns that line up with the WHEN section's operator and value-pill
 * columns above.
 *
 * Param values resolve to the *same* user-facing labels the rule editor
 * shows: enum codes → option labels, layout / algorithm UUIDs → layout
 * names, animation-event dotted paths → "Section · Event", shader-effect
 * ids → effect names, curves → CurvePresets display names, percent
 * fractions → "NN%". Anything unrecognised falls through to the raw
 * wire string so future kinds at least stay legible.
 */
ColumnLayout {
    id: root

    /// The actions list — `[{ type, ...params }, ...]`.
    required property var actionsJson
    /// Cached `controller.actionTypes()` table. Same caching rationale
    /// as `matchFieldOptions` in MatchExpressionView — the Q_INVOKABLE
    /// allocates a fresh list per call.
    required property var actionTypeOptions
    /// The composite app-settings surface — exposes `layouts` (for
    /// snapping / tiling resolution) and `animationsController` (for
    /// event / shader-effect resolution). Same object the rule editor
    /// receives. May be null while the page is still wiring things up
    /// — resolution falls back to the raw wire string in that case.
    property var appSettings: null
    /// Tree visualisation constants — kept in lockstep with
    /// MatchExpressionView's equivalents so the WHEN and THEN trees
    /// look like one consistent tree visualisation.
    readonly property real _indentStep: Kirigami.Units.gridUnit * 1.5
    readonly property color _guideColor: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
    // 1 device-independent px — matches MatchExpressionView's guide
    // thickness. See the rationale comment there.
    readonly property int _guideThickness: 1

    /// Look up the `{ value, label, params }` entry for a wire type
    /// string, or null when the type isn't in the registry.
    function _typeEntry(typeWire) {
        for (var i = 0; i < root.actionTypeOptions.length; ++i) {
            if (root.actionTypeOptions[i].value === typeWire)
                return root.actionTypeOptions[i];
        }
        return null;
    }

    /// Wire → label resolver for an action type. Falls back to the
    /// wire string when unknown (forward-compat with rules referencing
    /// action types this build doesn't ship).
    function _typeLabel(typeWire) {
        var e = root._typeEntry(typeWire);
        return e ? e.label : typeWire;
    }

    /// The param descriptor list for an action type (empty when
    /// unknown).
    function _paramsFor(typeWire) {
        var e = root._typeEntry(typeWire);
        return e ? e.params : [];
    }

    /// Resolve a single param value to the user-facing string the rule
    /// editor would show. The mapping per `kind` mirrors the editor's
    /// pickers — see `ActionRow.qml` for the source of truth.
    function _resolveParamValue(param, action) {
        var raw = action[param.key];
        if (raw === undefined || raw === null)
            return "";

        var kind = param.kind;
        var rawStr = String(raw);
        if (kind === "enum") {
            var opts = param.options || [];
            for (var i = 0; i < opts.length; ++i) {
                if (opts[i].value === raw)
                    return opts[i].label;
            }
            return rawStr;
        }
        if (kind === "snappingLayout" || kind === "tilingAlgorithm") {
            // Layouts are serialised via `toVariantMap(LayoutPreview)` which
            // stamps the friendly title under `displayName`. The previous
            // `.name` read returned undefined, leaving the read-only rule
            // row's value pill blank even when the layout existed in
            // `appSettings.layouts`. Fall back to the raw id when no
            // matching layout is found (hand-edited UUID typo, deleted
            // layout, etc.) so the user can SEE what the rule contains
            // rather than an empty pill.
            var layouts = root.appSettings && root.appSettings.layouts ? root.appSettings.layouts : [];
            // Snapping layouts are stored by UUID, which keys the layouts list
            // directly. Tiling-algorithm actions store the BARE registry token
            // ("bsp"), while the layouts list keys autotile entries as
            // "autotile:<token>" — so prefix before matching, mirroring the C++
            // summary resolver (SettingsController::resolveTilingAlgorithmLookup)
            // and the editor's `_tilingAlgorithmEditor`. Try the prefixed form
            // first, then the bare token (covering already-prefixed / bare-keyed
            // data) before falling back to the raw id.
            var candidates = [raw];
            if (kind === "tilingAlgorithm" && rawStr.indexOf("autotile:") !== 0)
                candidates = ["autotile:" + rawStr, rawStr];
            for (var c = 0; c < candidates.length; ++c) {
                for (var j = 0; j < layouts.length; ++j) {
                    if (layouts[j].id === candidates[c]) {
                        var name = layouts[j].displayName;
                        return name ? name : rawStr;
                    }
                }
            }
            return rawStr;
        }
        if (kind === "animationEvent") {
            var controller = root.appSettings ? root.appSettings.animationsController : null;
            if (!controller)
                return rawStr;

            var sections = controller.eventSections() || [];
            for (var s = 0; s < sections.length; ++s) {
                var paths = sections[s].paths || [];
                for (var p = 0; p < paths.length; ++p) {
                    if (paths[p].path === raw && !paths[p].isCategory)
                        return sections[s].label + " · " + paths[p].label;
                }
            }
            return rawStr;
        }
        if (kind === "shaderEffect") {
            var animCtl = root.appSettings ? root.appSettings.animationsController : null;
            if (!animCtl)
                return rawStr;

            var effects = animCtl.availableShaderEffects() || [];
            for (var k = 0; k < effects.length; ++k) {
                if (effects[k].id === raw)
                    return effects[k].name;
            }
            return rawStr;
        }
        if (kind === "decorationChain") {
            // Surface-pack ids resolve through the decoration pack catalog
            // (mirrors ActionRow's _decorationChainEditor source); unknown ids
            // render verbatim. An empty chain is the "no decoration" sentinel.
            var chainIds = raw || [];
            if (!chainIds.length)
                return i18n("Block decoration");
            var decoCtl = root.appSettings ? root.appSettings.decorationPage : null;
            var packs = decoCtl ? (decoCtl.availableShaderEffects() || []) : [];
            var names = [];
            for (var ci = 0; ci < chainIds.length; ++ci) {
                var packName = chainIds[ci];
                for (var pj = 0; pj < packs.length; ++pj) {
                    if (packs[pj].id === chainIds[ci]) {
                        packName = packs[pj].name;
                        break;
                    }
                }
                names.push(packName);
            }
            return names.join(", ");
        }
        if (kind === "overlayShader") {
            // Overlay shaders come from the snapping-shaders registry, not the
            // animation one (mirrors ActionRow's _overlayShaderEditor source).
            var ssCtl = root.appSettings ? root.appSettings.snappingShadersPage : null;
            if (!ssCtl)
                return rawStr;

            var overlayEffects = ssCtl.availableShaderEffects() || [];
            for (var oe = 0; oe < overlayEffects.length; ++oe) {
                if (overlayEffects[oe].id === raw)
                    return overlayEffects[oe].name;
            }
            return rawStr;
        }
        if (kind === "curveEditor") {
            // CurvePresets.curveLabel is the single source of truth for the
            // spring (`spring:omega,zeta`) + easing display name, shared with
            // the editor's curve button (ActionRow) and the C++ list resolver,
            // so this summary can't drift from them on the same wire value.
            return CurvePresets.curveLabel(rawStr);
        }
        if (kind === "percent") {
            var f = parseFloat(raw);
            if (isFinite(f)) {
                var scale = param.scale || 1;
                return Math.round(f / scale) + "%";
            }
            return rawStr;
        }
        if (kind === "zoneOrdinals") {
            // `raw` is a JS array of 1-based zone ordinals; render "1, 2".
            if (Array.isArray(raw))
                return raw.join(", ");
            return rawStr;
        }
        if (kind === "screenId") {
            // Show the friendly monitor label for the stored canonical id; fall
            // back to the raw id when the monitor isn't currently connected so the
            // user still sees what the rule pins to.
            var screens = root.appSettings && root.appSettings.screens ? root.appSettings.screens : [];
            for (var s = 0; s < screens.length; ++s) {
                if (screens[s].name === raw)
                    return screens[s].displayLabel || screens[s].name || rawStr;
            }
            return rawStr;
        }
        if (kind === "virtualDesktop") {
            // Render "N: <name>" when KWin reports a name for the 1-based desktop,
            // else just the number.
            var names = root.appSettings && root.appSettings.virtualDesktopNames ? root.appSettings.virtualDesktopNames : [];
            var num = parseInt(raw, 10);
            if (num >= 1 && names.length >= num && names[num - 1])
                return num + ": " + names[num - 1];
            return num > 0 ? String(num) : rawStr;
        }
        if (kind === "color") {
            // "accent" is the follow-the-system-accent sentinel; otherwise a
            // #AARRGGBB hex string. Show a readable word / upper-cased hex (the
            // actual swatch is rendered alongside this pill).
            return rawStr === "accent" ? i18n("Accent") : rawStr.toUpperCase();
        }
        if (kind === "bool") {
            // Render the JSON bool as On / Off for the value pill — without this
            // branch it fell through to the raw lowercase "true" / "false". The
            // collapsed rule list and the action editor's toggle caption use the
            // action's polarity phrase ("Hide border") instead; this terser On /
            // Off is the tabular pill form, matching the WHEN-side value pills.
            return (raw === true || raw === "true") ? i18n("On") : i18n("Off");
        }
        return rawStr;
    }

    // Spacing 0 — each delegate carries its own internal vertical
    // padding, and zero spacing between delegates lets the col-0 tree
    // verticals join up across rows without gaps.
    spacing: 0

    Repeater {
        model: root.actionsJson || []

        delegate: Item {
            id: actionDelegate

            required property var modelData
            required property int index
            // Snapshot of the action JSON — keeps the `modelData` name
            // free for the inner param Repeater's delegates.
            readonly property var _action: actionDelegate.modelData
            readonly property var _params: root._paramsFor(_action.type)
            // True for the bottom-most action — its tree-line vertical
            // stops at row-mid instead of running to the row bottom,
            // matching the "last child" terminator in WHEN.
            readonly property bool isLastAction: actionDelegate.index === (root.actionsJson ? root.actionsJson.length - 1 : 0)

            Layout.fillWidth: true
            implicitHeight: contentRow.implicitHeight + Kirigami.Units.gridUnit

            // Tree connector. Actions are flat (no nested composites),
            // so the geometry is always the depth-1 case — one column
            // vertical at x = indentStep/2 with an L-stub to the
            // bullet at content_left. Repaints when isLast or size
            // changes.
            Canvas {
                id: treeCanvas

                anchors.fill: parent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = root._guideColor;
                    ctx.lineWidth = root._guideThickness;
                    ctx.lineCap = "butt";
                    var x = Math.round(root._indentStep / 2) + 0.5;
                    var rowMid = Math.round(height / 2) + 0.5;
                    var contentLeft = root._indentStep + Kirigami.Units.smallSpacing;
                    // Vertical.
                    ctx.beginPath();
                    ctx.moveTo(x, 0);
                    ctx.lineTo(x, actionDelegate.isLastAction ? rowMid : height);
                    ctx.stroke();
                    // L-stub to the bullet.
                    ctx.beginPath();
                    ctx.moveTo(x, rowMid);
                    ctx.lineTo(contentLeft, rowMid);
                    ctx.stroke();
                }

                Connections {
                    function onIsLastActionChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: actionDelegate
                }

                // onPaint samples the theme-derived guide colour. Every
                // PlatformTheme colour shares the one `colorsChanged` notify
                // signal, so this one handler repaints on any palette change
                // (same pattern as CurveThumbnail).
                Connections {
                    function onColorsChanged() {
                        treeCanvas.requestPaint();
                    }

                    target: Kirigami.Theme
                }
            }

            RowLayout {
                id: contentRow

                anchors.left: parent.left
                anchors.leftMargin: root._indentStep + Kirigami.Units.smallSpacing
                anchors.right: parent.right
                anchors.rightMargin: Kirigami.Units.largeSpacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: Kirigami.Units.largeSpacing

                // Bullet — accent blue, same as WHEN leaf bullets.
                Rectangle {
                    Layout.alignment: Qt.AlignVCenter
                    implicitWidth: Math.round(Kirigami.Units.gridUnit / 2.5)
                    implicitHeight: implicitWidth
                    radius: width / 2
                    color: Kirigami.Theme.highlightColor
                }

                // Action type label — minWidth uses the same K=13gu
                // base column as WHEN's field column, minus one
                // indentStep so the type's right edge aligns with the
                // WHEN field's right edge at depth 1 (which is also
                // where every WHEN field column lands thanks to the
                // depth-compensating formula).
                Label {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.minimumWidth: Math.max(Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 13 - root._indentStep)
                    text: root._typeLabel(actionDelegate._action.type)
                    font.bold: true
                }

                // Per-parameter LABEL + value pill pairs. First param's
                // label sits at the same absolute x as WHEN's operator
                // column (because the type-label minWidth above
                // matches WHEN's depth-1 field width). Subsequent params
                // (`index > 0`) drop the 8 gridUnit floor and flow at
                // their text width so a short label like "SHADER EFFECT"
                // doesn't push its value pill into dead space — see the
                // index-gated `Layout.minimumWidth` on the inner Label.
                Repeater {
                    model: actionDelegate._params

                    delegate: RowLayout {
                        id: paramRow

                        required property var modelData
                        required property int index

                        spacing: Kirigami.Units.largeSpacing

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            // First param keeps the 8-gridUnit floor so its
                            // value pill lands in the same column as WHEN's
                            // value pills (visual alignment with the operator
                            // column above). Subsequent params shrink to
                            // their text width — leaving the 8 gu floor on
                            // every param turned the gap between a short
                            // label like "SHADER EFFECT" and its value pill
                            // into dead space because the label box itself
                            // padded out to 8 gu before the largeSpacing to
                            // the pill kicked in.
                            Layout.minimumWidth: paramRow.index === 0 ? Kirigami.Units.gridUnit * 8 : 0
                            text: paramRow.modelData.label
                            // One binding: a font.<sub> sibling next to a whole-group `font:` is an
                            // illegal duplicate binding that fails the whole document. FontUtils
                            // passes only the size dimension the theme font actually carries.
                            font: FontUtils.withProps(Kirigami.Theme.smallFont, {
                                capitalization: Font.AllUppercase
                            })
                            opacity: 0.55
                        }

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            implicitWidth: pillContent.implicitWidth + Kirigami.Units.largeSpacing * 2
                            implicitHeight: pillContent.implicitHeight + Kirigami.Units.smallSpacing
                            radius: Kirigami.Units.smallSpacing
                            Kirigami.Theme.colorSet: Kirigami.Theme.View
                            Kirigami.Theme.inherit: false
                            color: Kirigami.Theme.alternateBackgroundColor
                            border.width: 1
                            border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)

                            RowLayout {
                                id: pillContent

                                anchors.centerIn: parent
                                spacing: Kirigami.Units.smallSpacing

                                // Colour swatch for `color`-kind params — the raw
                                // value is a #AARRGGBB hex or the "accent" sentinel
                                // (resolved to the live accent colour for display).
                                Rectangle {
                                    readonly property string _rawColor: String(actionDelegate._action[paramRow.modelData.key] || "")

                                    visible: paramRow.modelData.kind === "color"
                                    Layout.alignment: Qt.AlignVCenter
                                    implicitWidth: valueLabel.implicitHeight
                                    implicitHeight: valueLabel.implicitHeight
                                    radius: Math.round(Kirigami.Units.smallSpacing / 2)
                                    color: paramRow.modelData.kind !== "color" ? "transparent" : (_rawColor === "accent" ? Kirigami.Theme.highlightColor : (_rawColor === "" ? Kirigami.Theme.backgroundColor : _rawColor))
                                    border.width: 1
                                    border.color: Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast)
                                }

                                Label {
                                    id: valueLabel

                                    Layout.alignment: Qt.AlignVCenter
                                    text: root._resolveParamValue(paramRow.modelData, actionDelegate._action)
                                    font.family: Kirigami.Theme.smallFont.family
                                }
                            }
                        }
                    }
                }

                // Stock-animation conflict chip — same shared component (and
                // therefore the same predicate and tree-suppression hide
                // condition) as the rule editor's action row, so the saved-rule
                // summary flags the conflict too, not just the editor.
                StockAnimationConflictChip {
                    action: actionDelegate._action
                    animationsController: root.appSettings ? root.appSettings.animationsController : null
                }

                Item {
                    Layout.fillWidth: true
                }
            }
        }
    }
}
