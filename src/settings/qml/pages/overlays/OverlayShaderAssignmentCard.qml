// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief One overlay shader assignment card: the global default (path "")
 *        or a per-layout override (path = layout UUID).
 *
 * Mirrors DecorationSurfaceCard's shape over the flat OverlayShaderTree:
 * the baseline card (isBaseline) has no toggle and always edits the
 * global default; a layout card's toggle opens the editor and writes
 * NOTHING (the _editorLatch pattern) — an override is created only on the
 * first real edit. OFF clears the override so the layout inherits the
 * global default again, and the card shows the resolved shader read-only
 * with a "Using global default" banner.
 *
 * Reactive-latch pattern: imperative refresh from the controller on
 * `shaderProfileChanged` / `shaderEffectsChanged`, not function bindings
 * that re-query C++ every repaint.
 *
 * Some strings here carry an i18nc context and some do not, and the split is
 * not an oversight to tidy up. A Qt Linguist catalogue is keyed on (source,
 * comment), so giving a shipped string a context retires the existing entry
 * and opens a fresh untranslated one. The uncontexted strings below are
 * already extracted and translated in seven languages. Give a NEW string its
 * context when you write it; do not retrofit one onto these.
 */
Item {
    id: root

    /// "" for the global default, a layout UUID (with braces) otherwise.
    required property string assignmentPath
    required property string cardLabel
    property bool isBaseline: false

    readonly property var bridge: settingsController.overlaysPage

    // Session-local "the user opened the editor" latch — see
    // DecorationSurfaceCard._editorLatch for the full rationale.
    property bool _editorLatch: false
    readonly property bool _editing: root.isBaseline || root._hasOverride || root._editorLatch

    // ── Reactive model state ─────────────────────────────────────────────
    property var _effects: []
    property bool _hasOverride: false
    property var _raw: ({})
    property var _resolved: ({})

    // The node the editor works on: the direct node when it exists, else
    // the resolved (inherited) node — a latched-open card with no override
    // starts from what the layout is actually drawing with.
    readonly property string _editShaderId: (root._hasOverride || root.isBaseline) ? (root._raw.shaderId || "") : (root._resolved.shaderId || "")
    readonly property var _editParams: (root._hasOverride || root.isBaseline) ? (root._raw.parameters || ({})) : (root._resolved.parameters || ({}))
    /// The preset this node points at, read the same way the parameters are:
    /// the direct override when there is one, else what the layout inherits,
    /// so a card with no override still shows the preset it draws with.
    readonly property string _editPresetId: (root._hasOverride || root.isBaseline) ? (root._raw.presetId || "") : (root._resolved.presetId || "")

    // Parameter DECLARATIONS for the shader being edited. Imperative like
    // the rest of the model state: a function-call binding on
    // shaderParameters() would not re-evaluate when the pack registry
    // rescans (shaderEffectsChanged), leaving stale declarations.
    property var _paramDefs: []

    /// The shader being edited names a pack this machine does not have. An
    /// absent pack and a pack with no parameters both leave _paramDefs empty,
    /// and only this tells the editor's empty state which one it is looking at.
    /// Same registry test _shaderName makes for its "Missing shader" label, so
    /// the two cannot disagree about the same id.
    readonly property bool _editShaderMissing: {
        if (root._editShaderId.length === 0)
            return false;
        for (var i = 0; i < root._effects.length; i++) {
            if (root._effects[i] && root._effects[i].id === root._editShaderId)
                return false;
        }
        return true;
    }

    // Each card fetches the pack list for itself, and availableShaderEffects
    // rebuilds it from the registry uncached, so N cards means N rebuilds.
    // Deliberate, and the same shape DecorationSurfaceCard uses, whose own
    // comment reasons about the same cost. The list is fetched once per card at
    // construction and again only when the registry actually rescans, not per
    // repaint or per parameter edit. Hoisting it to a page-level shared model
    // would couple every card's lifetime to the page's and buy nothing at the
    // handful of layouts this page ever shows. If it is ever worth doing, do it
    // for both card families at once rather than letting the two diverge.
    function _refreshEffects() {
        if (!root.bridge)
            return;
        root._effects = root.bridge.availableShaderEffects();
    }

    function _refreshParamDefs() {
        root._paramDefs = (root.bridge && root._editShaderId.length > 0) ? root.bridge.shaderParameters(root._editShaderId) : [];
    }

    function _paramsEqual(a, b) {
        if (a === b)
            return true;
        if (!a || !b)
            return false;
        for (var k in a) {
            if (a[k] !== b[k])
                return false;
        }
        for (var k2 in b) {
            if (!(k2 in a))
                return false;
        }
        return true;
    }

    function refresh() {
        if (!root.bridge)
            return;
        var wasOverride = root._hasOverride;
        // What the pending write was computed against, read before the state
        // below moves underneath it.
        var prevEditShaderId = root._editShaderId;
        var prevEditParams = root._editParams;

        var state = root.bridge.nodeState(root.assignmentPath);
        root._hasOverride = state.hasOverride;
        // An EXTERNAL clear (page reset/discard, a D-Bus write) closes the
        // latched editor; our own OFF path clears the latch before writing.
        if (wasOverride && !root._hasOverride)
            root._editorLatch = false;
        root._raw = state.raw;
        root._resolved = state.resolved;

        // Drop a pending debounced write whenever the node moved under it.
        //
        // The flush is 200ms behind the last drag tick, so anything that
        // rewrites this node in that window — a page Reset or Discard, a
        // profile apply, a D-Bus write, an external shader switch — would
        // otherwise be undone by a timer firing after the page already
        // reported clean, silently re-staging the value the user just
        // reverted and re-dirtying the page.
        //
        // Two independent signals, because neither covers the other.
        //
        // CONTENT is what a params-only revert moves, and a params-only revert
        // is exactly what Reset and Discard perform. It leaves the override in
        // place with the same shader id, so a structural check alone never
        // fires — and on the baseline card hasOverride is false on both sides
        // by construction, so a structural check is no signal there at all.
        //
        // EXISTENCE still matters on top of it: an external clear whose
        // baseline happens to carry the same shader and parameters is a
        // content no-op, and flushing into it would recreate the very override
        // the clear removed. The reverse direction counts too — an override
        // appearing externally means the pending write was computed against
        // the inherited node, which is no longer what this card edits.
        if (root._pendingParams !== null && (wasOverride !== root._hasOverride || prevEditShaderId !== root._editShaderId || !root._paramsEqual(prevEditParams, root._editParams)))
            root._dropPendingParams();

        root._refreshParamDefs();
    }

    function _shaderName(id) {
        // Contexted because "None" agrees with its noun in several target
        // languages — German alone splits Keiner / Keine / Keines across the
        // other sites — so an uncontexted entry would collapse them all onto
        // one form.
        if (!id || id.length === 0)
            return i18nc("@item no overlay shader assigned", "None");
        for (var i = 0; i < root._effects.length; i++) {
            if (root._effects[i] && root._effects[i].id === id)
                return root._effects[i].name;
        }
        return i18nc("@item the assigned overlay shader pack is not installed", "Missing shader %1", id);
    }

    // Write the whole node for this path. A latched card with no override
    // creates one here (the first real edit).
    function _writeNode(shaderId, params) {
        if (!root.bridge)
            return;
        root.bridge.setShaderOverride(root.assignmentPath, shaderId, params);
    }

    // Slider drags emit a value per mouse-move tick; each write walks the
    // whole tree and refreshes every card, so coalesce parameter edits
    // behind a short timer instead of writing per tick. The shader id is
    // captured at enqueue time so a pack switch racing the flush cannot
    // pair old parameters with a new shader (the switch also drops any
    // pending edit, since it resets parameters anyway).
    property var _pendingParams: null
    property string _pendingShaderId: ""

    function _queueParamWrite(shaderId, paramId, value) {
        var base = (root._pendingParams !== null && root._pendingShaderId === shaderId) ? root._pendingParams : root._editParams;
        var next = ({});
        for (var k in base)
            next[k] = base[k];
        next[paramId] = value;
        root._pendingParams = next;
        root._pendingShaderId = shaderId;
        paramWriteDebounce.restart();
    }

    function _dropPendingParams() {
        paramWriteDebounce.stop();
        root._pendingParams = null;
        root._pendingShaderId = "";
    }

    Timer {
        id: paramWriteDebounce

        interval: 200
        onTriggered: {
            if (root._pendingParams === null)
                return;
            var params = root._pendingParams;
            var shaderId = root._pendingShaderId;
            root._pendingParams = null;
            root._pendingShaderId = "";
            root._writeNode(shaderId, params);
        }
    }

    implicitHeight: card.implicitHeight
    Component.onCompleted: {
        root._refreshEffects();
        root.refresh();
    }
    Component.onDestruction: {
        // Flush a still-pending debounced edit — the Timer dies with the
        // card, and the UI already showed the dragged value. The bridge
        // lives on settingsController and outlives the card.
        if (root._pendingParams !== null && root.bridge)
            root._writeNode(root._pendingShaderId, root._pendingParams);
    }

    Connections {
        target: root.bridge
        function onShaderProfileChanged(path, wholeTree) {
            // A single-node write only concerns this card when it IS that
            // node, or when the baseline moved and this card has no override
            // of its own — the baseline is what it resolves to then. Without
            // this every card on the page re-ran a full refresh for every
            // parameter commit on any other card.
            if (wholeTree || path === root.assignmentPath || (path.length === 0 && !root._hasOverride))
                root.refresh();
        }
        function onShaderEffectsChanged() {
            root._refreshEffects();
            root.refresh();
        }
    }

    SettingsCard {
        id: card

        anchors.fill: parent
        headerText: root.cardLabel
        collapsible: true
        showToggle: !root.isBaseline
        toggleChecked: root._editing
        gateBodyOnToggle: false
        onToggleClicked: function (checked) {
            if (checked) {
                // Open the editor, write nothing — an override is created
                // only when the user actually picks a shader or edits a
                // parameter.
                root._editorLatch = true;
            } else {
                root._editorLatch = false;
                root._dropPendingParams();
                if (root.bridge)
                    root.bridge.clearOverride(root.assignmentPath);
            }
        }

        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            Kirigami.InlineMessage {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                type: Kirigami.MessageType.Information
                visible: !root.isBaseline && !root._editing
                // Uncontexted and plural to match the already-translated string
                // the animation and decoration cards use.
                text: i18n("Using global defaults")
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: !root._editing
                text: i18n("Current: %1", root._shaderName(root._resolved.shaderId || ""))
                font.italic: true
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.largeSpacing
                Layout.rightMargin: Kirigami.Units.largeSpacing
                visible: root._editing
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.largeSpacing

                    Label {
                        // Keeps the @label context the deleted editor dialog
                        // used, so the shipped translations still match.
                        text: i18nc("@label", "Shader:")
                    }

                    PZCommon.CategoryMenuButton {
                        Layout.fillWidth: true
                        items: root._effects
                        currentId: root._editShaderId
                        includeNoneEntry: true
                        // Left uncontexted on purpose: this exact string is
                        // already translated, and adding a context would re-key
                        // the entry and drop those translations.
                        placeholderText: i18n("Choose an overlay shader…")
                        Accessible.name: root.isBaseline ? i18nc("@label:listbox", "Global default overlay shader") : i18nc("@label:listbox overlay shader for a named layout", "Overlay shader for %1", root.cardLabel)
                        onSelected: function (id) {
                            // Switching packs resets the parameters to the new
                            // pack's defaults (an empty override map).
                            root._dropPendingParams();
                            root._writeNode(id, ({}));
                        }
                    }
                }

                // Overlays embed the params editor directly rather than going
                // through PackEditorBody (there is no preview here), so the
                // preset row is added alongside instead of riding in with it.
                PresetRow {
                    Layout.fillWidth: true
                    visible: root._editShaderId.length > 0
                    packId: root._editShaderId
                    presetBridge: settingsController.overlayPresets
                    presetId: root._editPresetId
                    currentValues: root._editParams
                    onPresetSelected: function (id) {
                        settingsController.overlaysPage.setShaderPreset(root.assignmentPath, id);
                        root.refresh();
                    }
                    onRevertRequested: {
                        // Dropping the deltas is the whole revert: every value
                        // then resolves from the preset again.
                        root._dropPendingParams();
                        root._writeNode(root._editShaderId, ({}));
                    }
                }

                PZCommon.ShaderParamsEditor {
                    Layout.fillWidth: true
                    visible: root._editShaderId.length > 0
                    parameters: root._paramDefs
                    currentValues: root._editParams
                    effectId: root._editShaderId
                    subjectMissing: root._editShaderMissing
                    enableLocking: true
                    enableRandomize: true
                    enableImage: false
                    compact: true
                    onValueChanged: function (effectId, paramId, value) {
                        root._queueParamWrite(effectId, paramId, value);
                    }
                    onRandomizeRequested: function (rolled) {
                        root._dropPendingParams();
                        root._writeNode(root._editShaderId, rolled);
                    }
                    onResetRequested: function (defaults) {
                        root._dropPendingParams();
                        root._writeNode(root._editShaderId, defaults);
                    }
                }
            }
        }
    }
}
