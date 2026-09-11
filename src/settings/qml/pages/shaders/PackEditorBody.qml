// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief The body every "configure one pack" surface shows: its parameters
 * beside a live preview of it.
 *
 * Used by the decoration and pointer chain rows (through ChainEditor's row
 * expansion) and by the animation event card. Those are the three places a
 * pack is tuned, and before this they each laid the same two children out
 * themselves.
 *
 * ## Side by side, not stacked
 *
 * The editor takes the left column and the preview is pinned right at its own
 * width, which is the arrangement the pack browser's detail dialog already
 * uses. Stacked, the preview's fixed canvas floated centred with a wide empty
 * band down either side of it, and the parameter rows stretched the full
 * window until each label and its slider sat at opposite ends.
 *
 * The preview gets a fixed column rather than the remaining width because it
 * composes at one canvas size and only ever scales DOWN (see PackPreview), so
 * handing it more room never enlarged it, it only pushed it further from the
 * controls it belongs to.
 *
 * Two columns only while there is room for both. Below that the grid folds to
 * one, because a preview column wide enough to be worth showing would leave
 * the editor too narrow to read.
 */
GridLayout {
    id: root

    // ── Preview ──────────────────────────────────────────────────────────
    /// Which family's preview to render. Empty renders none, which is what
    /// the rules-action embed wants: a rule chain is edited against no
    /// particular surface and has no controller to render one with.
    property string previewKind: ""
    /// The family's preview controller. Empty kind or null controller both
    /// mean no preview.
    property QtObject previewController: null
    /// Gates the preview's EXISTENCE, so a collapsed host instantiates no
    /// shader item. Hosts pass their own expansion state.
    property bool previewActive: false
    /// Freezes the preview's clock while the settings window is not in front.
    /// Separate from `previewActive`, which destroys the item: an expanded row
    /// on a backgrounded window should keep its composition and stop ticking,
    /// not tear down and rebuild.
    readonly property bool _appActive: Qt.application.state === Qt.ApplicationActive

    // ── The pack ─────────────────────────────────────────────────────────
    /// The pack being configured. Identifies it to both children.
    required property string packId
    /// The pack's declared parameter schema.
    property var parameters: []
    /// The user's current values for them.
    property var currentValues: ({})
    /// The editor owns the lock map and self-updates it; a host that resets
    /// locks on a pack switch writes straight through this.
    property alias lockedParams: paramEditor.lockedParams

    // ── Editor affordances ───────────────────────────────────────────────
    property bool enableGroups: false
    property bool enableLocking: true
    property bool enableRandomize: true
    property bool enableReset: true
    property bool enableImage: false

    // ── Presets ──────────────────────────────────────────────────────────
    /// The family's `ShaderPresetBridge`. Null hides the preset row, which is
    /// what a host with no preset support passes.
    property QtObject presetBridge: null
    /// The assignment's current preset id, or empty for none.
    property string presetId: ""

    /// What the pack actually renders with: the assigned preset's values with
    /// this assignment's own edits laid over the top.
    ///
    /// `currentValues` holds only the DELTAS, because that is what an
    /// assignment stores. Feeding those straight to the sliders and the preview
    /// showed a pack's plain defaults for every parameter the preset supplies,
    /// so picking a preset looked like it had done nothing at all.
    ///
    /// Imperative rather than bound, like every other registry-backed value in
    /// this app: the preset lives on disk, so a binding would never re-evaluate
    /// when it is retuned.
    property var _effectiveValues: ({})

    function _recomputeEffective() {
        const deltas = root.currentValues || {};
        if (!root.presetBridge || root.presetId.length === 0 || root.packId.length === 0) {
            root._effectiveValues = deltas;
            return;
        }
        // The merge is `ShaderPresetRegistry::resolveParams`, reached through the
        // bridge, so a preview cannot disagree with what the compositor will
        // render — and it applies the declared-range clamp, which the hand-written
        // JS overlay this replaced knew nothing about. It is also one definition
        // instead of the three that had each been written out separately.
        //
        // One call per change, including per drag tick. That is cheaper than what it
        // replaces: the JS version called `presetParams`, which returns a
        // `ShaderPreset` BY VALUE and copied its whole parameter map, and then built
        // the merged object in JS on top of that.
        root._effectiveValues = root.presetBridge.effectiveParams(root.packId, root.presetId, deltas);
    }

    /// `{ paramId: true }` for every key this assignment stores of its own, and
    /// empty with no preset engaged (nothing is "on top of" anything, so marking
    /// every row would say nothing).
    readonly property var _overriddenParams: {
        if (!root.presetId || root.presetId.length === 0)
            return ({});
        const marks = {};
        const deltas = root.currentValues || {};
        for (const key in deltas)
            marks[key] = true;
        return marks;
    }

    onCurrentValuesChanged: root._recomputeEffective()
    onPresetIdChanged: root._recomputeEffective()
    onPackIdChanged: root._recomputeEffective()
    onPresetBridgeChanged: root._recomputeEffective()
    Component.onCompleted: root._recomputeEffective()

    // A preset retuned anywhere (this window, another process, a text editor)
    // has to move what is on screen here too.
    Connections {
        target: root.presetBridge
        function onPresetsChanged(packId) {
            if (packId === root.packId)
                root._recomputeEffective();
        }
    }

    signal valueChanged(string effectId, string paramId, var value)
    signal randomizeRequested(var rolled)
    signal resetRequested(var defaults)
    /// The user picked a different preset. The host writes it to the
    /// assignment; nothing is persisted here.
    signal presetSelected(string presetId)
    /// The assignment's own parameter edits should be dropped, so every value
    /// goes back to following the preset.
    signal presetRevertRequested
    /// The selected preset was deleted. The host should clear its stored reference
    /// and leave the parameter values alone.
    signal presetDeleted(string presetId)

    // Wide enough that a preview lands at 1:1 rather than being reduced to fit,
    // at any font scale. A pane-shaped preview frames the canvas, so the column
    // has to carry the canvas plus that frame; derived from the canvas rather
    // than from grid units alone, because at a small grid unit a purely
    // grid-derived column is narrower than the canvas and silently reduces.
    readonly property real _previewWidth: Math.max(Kirigami.Units.gridUnit * 24, PreviewCanvas.size.width + Kirigami.Units.gridUnit)
    readonly property bool _hasParams: (root.parameters || []).length > 0
    readonly property bool _hasPreview: root.previewKind.length > 0 && root.previewController !== null && root.packId.length > 0
    // Two columns need both children AND room for the preview's column twice
    // over, so the editor keeps at least as much width as the preview.
    readonly property bool _twoColumn: _hasParams && _hasPreview && width >= root._previewWidth * 2

    columns: root._twoColumn ? 2 : 1
    columnSpacing: Kirigami.Units.largeSpacing
    rowSpacing: Kirigami.Units.smallSpacing

    PZCommon.ShaderParamsEditor {
        id: paramEditor

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        visible: root._hasParams
        compact: true
        parameters: root.parameters
        currentValues: root._effectiveValues
        // The editor is fed the MERGED values, so the preset's values and this
        // assignment's own edits would otherwise render identically. The delta
        // map IS the set of own edits — presence in it is the pin, equal value
        // included — so it needs no comparison to derive.
        overriddenParams: root._overriddenParams
        effectId: root.packId
        enableGroups: root.enableGroups
        enableLocking: root.enableLocking
        enableRandomize: root.enableRandomize
        enableReset: root.enableReset
        enableImage: root.enableImage
        onValueChanged: function (effectId, paramId, value) {
            root.valueChanged(effectId, paramId, value);
        }
        onRandomizeRequested: function (rolled) {
            root.randomizeRequested(rolled);
        }
        onResetRequested: function (defaults) {
            root.resetRequested(defaults);
        }
    }

    // The preview column: the stage, and under it whatever the family has to
    // say about the pack. Today only the pointer family says anything.
    ColumnLayout {
        // Fixed column when beside the editor; centred and no wider than its
        // canvas when stacked, so a narrow host does not stretch it.
        Layout.preferredWidth: root._twoColumn ? root._previewWidth : Math.min(root.width, root._previewWidth)
        Layout.alignment: Qt.AlignTop | (root._twoColumn ? Qt.AlignRight : Qt.AlignHCenter)
        visible: root._hasPreview
        spacing: Kirigami.Units.smallSpacing

        PackPreview {
            Layout.fillWidth: true
            previewKind: root.previewKind
            previewController: root.previewController
            packId: root.packId
            params: root._effectiveValues
            active: root._hasPreview && root.previewActive
            // `active` covers "this row is collapsed" — it tears the shader item
            // down. This covers "the window is not in front": a chain row left
            // expanded on a page the user navigated away from stays instantiated
            // (the page host keeps a visited page active and only hides it), so
            // without this its 60 Hz clock keeps running against a preview nobody
            // can see. Same lever the detail dialog already uses.
            animating: root._appActive
        }

        // A pointer pack's cursor-order and stand-in notices, the same strip
        // the browser's detail pane shows, so a chain row does not hide what
        // the catalogue tells the user about the pack it is tuning. Handed a
        // controller only for the pointer kind: the strip reads packInfo on
        // it, which only the pointer controller answers in pointer terms, and
        // it collapses to nothing with a null one.
        PointerPackNotices {
            Layout.fillWidth: true
            previewController: root.previewKind === "pointer" ? root.previewController : null
            packId: root.packId
        }
    }

    // Last child, so the grid places it in the next row's FIRST column: under
    // the parameters and no wider than they are.
    //
    // Matches the pack browser's detail dialog, which puts the same row along
    // the bottom of its params column. It used to sit above the editor and span
    // both columns, which ran it under the preview and made one card disagree
    // with the other about where presets live.
    PresetRow {
        Layout.fillWidth: true
        packId: root.packId
        presetBridge: root.presetBridge
        presetId: root.presetId
        currentValues: root._effectiveValues
        // The assignment's own stored map, so the row can answer "does this carry
        // a delta" rather than comparing merged values — which cannot see a delta
        // pinned at the preset's own value, nor one on a parameter the preset
        // does not mention.
        deltas: root.currentValues
        onPresetSelected: function (id) {
            root.presetSelected(id);
        }
        onPresetDeleted: function (id) {
            root.presetDeleted(id);
        }
        onRevertRequested: root.presetRevertRequested()
    }
}
