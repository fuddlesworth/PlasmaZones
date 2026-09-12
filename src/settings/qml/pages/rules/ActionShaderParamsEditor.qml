// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.plasmazones.common as PZCommon

/**
 * @brief The shader-uniform editor shared by both shader-override rule actions.
 *
 * Its own file rather than one more Component inside ActionParamEditors.qml, which
 * is past the size ceiling — the same move ActionPresetEditor.qml made, and for the
 * same reason.
 *
 * Bound to `row._activeShaderParamSchema`, which selects the matching registry's
 * schema per action type (OverrideAnimationShader or OverrideOverlayShader). The
 * action stores a nested `params` object and changing any value rewrites the whole
 * object. Locks live on the row as working state and are not persisted, exactly
 * like the per-event card on the animations page. Randomize rolls a new map
 * respecting locks. Image picking is off because shader-image uniforms are not part
 * of the rule wire format.
 *
 * ## The preset axis
 *
 * This editor has to MERGE, not just display. The compositor resolves preset ⊕
 * params for both shader actions, so `action.params` is a delta map, and feeding it
 * straight to the sliders showed the pack's plain defaults for every parameter the
 * preset supplies — picking a preset in a rule row looked like it had done nothing.
 * That is the same defect the assignment hosts carry `_effectiveValues` for, and
 * this was the last editor without it.
 */
PZCommon.ShaderParamsEditor {
    id: paramEditor

    /// The ActionRow this editor belongs to. Passed in rather than reached for
    /// through parent chains, which is what the in-file Component could rely on and
    /// a standalone type cannot. Typed, not `var`, for the same reason
    /// ActionPresetEditor's copy is.
    required property ActionRow row

    /// What the pack actually renders with: the named preset's values with
    /// this rule's own edits laid over the top.
    ///
    /// `action.params` holds only the DELTAS, and the compositor resolves
    /// preset ⊕ params for both shader actions (see the rule route in
    /// daemon/overlayservice/shader.cpp). Feeding the deltas straight to the
    /// sliders showed the pack's plain defaults for every parameter the
    /// preset supplies, so picking a preset in a rule row looked inert —
    /// exactly the bug the assignment hosts have `_effectiveValues` for.
    ///
    /// Imperative, like every other registry-backed value in this app: the
    /// preset lives on disk, so a binding would never re-evaluate when it is
    /// retuned.
    property var _effectiveValues: ({})

    function _recomputeEffective() {
        const deltas = row.action.params || row._emptyShaderParams;
        const packId = row.action.effectId || "";
        if (!row._shaderPresetBridge || row._shaderPresetId.length === 0 || packId.length === 0) {
            paramEditor._effectiveValues = deltas;
            return;
        }
        // The bridge's own merge, which is ShaderPresetRegistry::resolveParams
        // — the same one the daemon resolves this rule through, so the row
        // cannot disagree with what renders. It also applies the pack's
        // declared-range clamp.
        paramEditor._effectiveValues = row._shaderPresetBridge.effectiveParams(packId, row._shaderPresetId, deltas);
    }

    /// The delta KEY SET as a stable string, so the marks below rebuild when
    /// the set CHANGES rather than on every value write. Same reason as
    /// PackEditorBody's twin.
    readonly property string _deltaKeySignature: {
        if (row._shaderPresetId.length === 0)
            return "";
        return Object.keys(row.action.params || row._emptyShaderParams).sort().join(",");
    }

    /// `{ paramId: true }` for every key this rule stores of its own, empty
    /// with no preset engaged. A rule has no ancestor to inherit from, so its
    /// stored map IS its own map.
    readonly property var _overriddenParams: {
        const signature = paramEditor._deltaKeySignature;
        if (signature.length === 0)
            return ({});
        const marks = {};
        for (const key of signature.split(","))
            marks[key] = true;
        return marks;
    }

    parameters: row._activeShaderParamSchema
    currentValues: paramEditor._effectiveValues
    overriddenParams: paramEditor._overriddenParams
    effectId: row.action.effectId || ""
    enableLocking: true
    enableRandomize: true
    enableImage: false
    compact: true

    Component.onCompleted: paramEditor._recomputeEffective()
    onParametersChanged: paramEditor._recomputeEffective()

    // `row.action` is replaced wholesale on every write, so this is what
    // carries a delta edit, a preset pick and a pack switch alike into the
    // merged view.
    Connections {
        target: row
        function onActionChanged() {
            paramEditor._recomputeEffective();
        }
    }

    // A preset retuned anywhere — this window, another process, a text
    // editor — has to move what this row shows too.
    Connections {
        target: row._shaderPresetBridge
        function onPresetsChanged(packId) {
            if (packId === (row.action.effectId || ""))
                paramEditor._recomputeEffective();
        }
    }
    // The shared editor owns the session-only lock map and hosts the
    // colour dialog; the rule only persists values. Locks reset on
    // effect switch via the hosting Loader in ActionRow.qml (its
    // Connections handler lives there, not in this Component file).
    onValueChanged: function (effectId, paramId, value) {
        // Clone the current param map and stamp the new value so the
        // binding re-evaluates (mutating in place wouldn't trigger).
        var next = ({});
        var existing = row.action.params || ({});
        for (var k in existing)
            next[k] = existing[k];
        next[paramId] = value;
        row.actionEdited(row._withParam("params", next));
    }
    onRandomizeRequested: function (rolled) {
        // computeRandomized respects locks: locked params keep their
        // current value, the rest are rolled per their schema range.
        row.actionEdited(row._withParam("params", rolled));
    }
    onResetRequested: function (defaults) {
        // With a preset engaged, "reset" means FOLLOW THE PRESET, so the
        // deltas are dropped rather than replaced with the pack defaults.
        // Writing the defaults map would pin every parameter as a delta on
        // top of the preset, which is the opposite of what the button says —
        // the same guard the overlay card and the decoration card each apply.
        if (row._shaderPresetId.length > 0) {
            row.actionEdited(row._withParam("params", ({})));
            return;
        }
        // No preset: the full defaults map replaces the rule's param
        // overrides in one write, mirroring the randomize batch above.
        row.actionEdited(row._withParam("params", defaults));
    }
}
