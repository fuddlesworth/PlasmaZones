// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief One editable action row inside ActionListEditor.
 *
 * An action is a `{ type, ...params }` JSON object. The row exposes a type
 * picker and one editor per parameter, both driven entirely by the
 * `actionTypeOptions` metadata from `RuleController.actionTypes()` —
 * there is no per-type `if (t === "...")` ladder here. Two-way: edits emit
 * `actionEdited(updatedAction)`; the parent owns the list.
 *
 * For `overrideAnimationShader` and `overrideOverlayShader`, a
 * `ShaderParamsEditor` surfaces below the row when an effect is selected so
 * shader uniforms can be edited in place — matching the animation-settings
 * page's per-event editor. Each shader-override type sources its uniform schema
 * from its own registry (animation vs overlay/snapping).
 */
ColumnLayout {
    id: row

    /// The action JSON object being edited — `{ type, ...params }`.
    required property var action
    /// The RuleController — exposes `defaultPayloadFor(typeWire)` so a
    /// type switch can pre-seed the new param set in one place (matches the
    /// shape ActionListEditor uses when appending a fresh action).
    required property var controller
    /// Registered action types from `RuleController.actionTypes()` —
    /// each entry: `{ value, label, params: [{ key, kind, label, ... }],
    /// domain: "context"|"window" }`. The `domain` field is consulted by the
    /// row-level incompatibility flag below (`_currentTypeIncompatible`).
    required property var actionTypeOptions
    /// The SettingsController — drives the snappingLayout / tilingAlgorithm
    /// picker dropdowns.
    required property var appSettings
    /// True when the current match references only context fields — passed in
    /// from ActionListEditor. Drives `_currentTypeIncompatible`: a context-
    /// domain action against a window-property match is flagged with the
    /// per-row warning chip + the sheet's InlineMessage (the combination that
    /// silently never fires), while the picker itself stays a plain menu.
    required property bool matchIsContextOnly
    /// True when the action's currently-selected type is incompatible with
    /// the rule's match (a context action against a window-property match).
    /// Surfaces a small inline warning next to the type picker so the user
    /// sees the mismatch on the existing action even before opening the
    /// type menu — mirrors the RuleEditorSheet's whole-rule
    /// InlineMessage but scoped to this single action row.
    readonly property bool _currentTypeIncompatible: {
        if (row._typeEntry === undefined)
            return false;

        return row._typeEntry.domain === "context" && !row.matchIsContextOnly;
    }
    /// The descriptor for the current action's type, or undefined if unknown.
    readonly property var _typeEntry: row._entryForType(row.action.type)
    /// Parameter descriptors for the current type (empty when none / unknown).
    readonly property var _params: row._typeEntry !== undefined ? row._typeEntry.params : []
    /// Combined input-format hint(s) for the current params — the optional
    /// `param.hint` strings from the action metadata (ruleauthoring's
    /// paramHint), joined one per line. Empty when no param carries a hint.
    /// Surfaces the accepted syntax (e.g. zone-number lists / ranges) that a
    /// placeholder can't show once the field holds a value; there is no
    /// per-type ladder here — a param gets a hint only if its descriptor does.
    readonly property string _paramHint: {
        var parts = [];
        for (var i = 0; i < row._params.length; i++) {
            var h = row._params[i].hint;
            if (h !== undefined && h.length > 0)
                parts.push(h);
        }
        return parts.join("\n");
    }
    /// Shader-uniform schema for the action's currently-selected effect. Empty
    /// when the action is not a shader-override, no effect is set, or the
    /// effect declares no parameters. Drives the inline shader editor below
    /// the row.
    readonly property var _shaderParamSchema: {
        if (row.action.type !== "overrideAnimationShader")
            return [];

        var effectId = row.action.effectId || "";
        if (effectId.length === 0)
            return [];

        var controller = row.appSettings ? row.appSettings.animationsController : null;
        return controller ? controller.shaderParameters(effectId) : [];
    }
    /// Shader-uniform schema for OverrideOverlayShader — same shape as
    /// `_shaderParamSchema` but sourced from the overlay/snapping shader
    /// registry (the catalog entry's `parameters`), not the animation one.
    readonly property var _overlayShaderParamSchema: {
        if (row.action.type !== "overrideOverlayShader")
            return [];

        var effectId = row.action.effectId || "";
        if (effectId.length === 0)
            return [];

        var controller = row.appSettings ? row.appSettings.snappingShadersPage : null;
        if (!controller)
            return [];

        var effects = controller.availableShaderEffects() || [];
        for (var i = 0; i < effects.length; ++i) {
            if (effects[i].id === effectId)
                return effects[i].parameters || [];
        }
        return [];
    }
    /// The active shader-uniform schema for whichever shader-override action is
    /// being edited (animation or overlay) — drives the inline
    /// ShaderParamsEditor below the row.
    readonly property var _activeShaderParamSchema: {
        if (row.action.type === "overrideAnimationShader")
            return row._shaderParamSchema;
        if (row.action.type === "overrideOverlayShader")
            return row._overlayShaderParamSchema;
        return [];
    }
    /// The target algorithm id for a SetAlgorithmParam action, or "" otherwise.
    /// A `string` property so its change signal fires only when the VALUE changes —
    /// `_withParam` replaces the whole `row.action` object on every nested-param
    /// write, so a binding reading `row.action.algorithm` directly would re-fire on
    /// each write; routing the schema through this value-stable id keeps the params
    /// editor from rebuilding its sliders mid-drag (matching the shader editor,
    /// whose schema binding returns a cached-identity array).
    readonly property string _algorithmParamAlgoId: (row.action.type === "setAlgorithmParam" && row.action.algorithm) ? row.action.algorithm : ""
    /// The custom-parameter schema for the SetAlgorithmParam action's currently
    /// selected algorithm — drives the inline algorithm-params editor below the
    /// row (the autotile analogue of _activeShaderParamSchema). Returns the
    /// Luau-declared param descriptors ({name, type, value, defaultValue,
    /// minValue, maxValue, enumOptions, description}); empty until an algorithm
    /// is picked. Depends only on _algorithmParamAlgoId (not row.action), so it
    /// re-resolves on algorithm change, not on every param write.
    readonly property var _activeAlgorithmParamSchema: {
        if (row._algorithmParamAlgoId.length > 0 && row.appSettings && row.appSettings.tilingAlgorithmPage)
            return row.appSettings.tilingAlgorithmPage.customParamsForAlgorithm(row._algorithmParamAlgoId);
        return [];
    }
    /// The algorithm schema adapted to the shared ParameterEditor's descriptor
    /// shape: the algorithm's `name` is a code identifier so it becomes the
    /// `id` (value key), and its human `description` becomes the display
    /// `name`; `minValue`/`maxValue`/`defaultValue` map to `min`/`max`/
    /// `default`. Depends only on the value-stable schema, so it rebuilds on
    /// algorithm change, not on every param write.
    readonly property var _adaptedAlgorithmParamSchema: {
        var schema = row._activeAlgorithmParamSchema || [];
        var out = [];
        for (var i = 0; i < schema.length; i++) {
            var p = schema[i];
            if (!p || p.name === undefined)
                continue;

            out.push({
                "id": p.name,
                "name": (p.description && p.description.length > 0) ? p.description : p.name,
                "type": p.type,
                "default": p.defaultValue,
                "min": p.minValue,
                "max": p.maxValue,
                "enumOptions": p.enumOptions
            });
        }
        return out;
    }
    /// The live id→value map ParameterEditor renders: the rule's stored
    /// override wins, else the algorithm's saved value, else its default.
    /// Reassigned (fresh identity) on every write so the editor re-syncs.
    readonly property var _algorithmParamValues: {
        var vals = {};
        var schema = row._activeAlgorithmParamSchema || [];
        var overrides = (row.action && row.action.params) ? row.action.params : {};
        for (var i = 0; i < schema.length; i++) {
            var p = schema[i];
            if (!p || p.name === undefined)
                continue;

            vals[p.name] = overrides[p.name] !== undefined ? overrides[p.name] : (p.value !== undefined ? p.value : p.defaultValue);
        }
        return vals;
    }
    /// Write one custom-param value into the action's nested `params` object
    /// (shallow-cloned so the binding re-triggers), mirroring the shader editor's
    /// per-uniform write via _withParam("params", …).
    function _writeAlgorithmParam(name, value) {
        var next = {};
        if (row.action.params)
            for (var k in row.action.params)
                next[k] = row.action.params[k];
        next[name] = value;
        row.actionEdited(row._withParam("params", next));
    }
    /// Stable empty-object fallback for the inline shader params editor's
    /// `currentValues` binding — using `({})` inline would allocate a new
    /// object identity per binding evaluation and churn the editor.
    readonly property var _emptyShaderParams: ({})
    // The OverrideDecorationChain action payload has a fixed two-key schema: the
    // ordered pack list under "chain", the per-pack overrides under "params".
    // The inline add-picker and the below-row list editor live in different
    // scopes (the list editor's Loader has no param descriptor), so naming the
    // keys once here keeps the two halves from drifting onto different literals.
    readonly property string _decorationChainKey: "chain"
    readonly property string _decorationParamsKey: "params"
    // Param-editor Components — one per param `kind`, keyed off `parent.modelData`
    // (the hosting Loader's descriptor). They live in ActionParamEditors.qml
    // (instantiated below as `paramEditors`) so this file stays under the
    // file-size ceiling; the Loaders resolve each template via
    // `paramEditors._…ParamEditor`. Purely a home for the templates — they still
    // read `row.*` for state through the `row: row` handle.
    property ActionParamEditors paramEditors: ActionParamEditors {
        row: row
    }

    /// Encode a QML color to a `#AARRGGBB` wire string (alpha-first) — the form
    /// the border-colour validator accepts and the consumer parses back via
    /// QColor::HexArgb. Mirrors how general-settings border colours are stored.
    function _toHexArgb(c) {
        function h(v) {
            var s = Math.round(v * 255).toString(16);
            return s.length < 2 ? "0" + s : s;
        }
        return "#" + h(c.a) + h(c.r) + h(c.g) + h(c.b);
    }

    // `actionEdited`, not `actionChanged`, because `property var action`
    // auto-generates `actionChanged()` and QML rejects the duplicate signal.
    signal actionEdited(var updatedAction)
    signal removeRequested

    /// Shallow-clone the action so a mutation produces a fresh object QML
    /// rebinds against (mutating in place would not re-trigger bindings).
    function _withParam(key, value) {
        var next = {};
        for (var k in row.action)
            next[k] = row.action[k];
        next[key] = value;
        return next;
    }

    /// Build a fresh payload for the type switch — pre-seeded by the
    /// controller's `defaultPayloadFor` (the same call ActionListEditor
    /// uses when appending) and then any param key the previous and new
    /// types share is migrated from the existing action so the user
    /// doesn't lose, say, a chosen `event` when switching between two
    /// animation overrides. The migration is intentionally conservative:
    /// only when both descriptors declare the **same key** AND the same
    /// `kind` does the value carry over — different `kind`s could
    /// otherwise leak the wrong shape (a `snappingLayout` UUID into a
    /// `tilingAlgorithm` slot).
    function _payloadForType(newType) {
        var payload = row.controller ? row.controller.defaultPayloadFor(newType) : ({
                "type": newType
            });
        var newEntry = row._entryForType(newType);
        var newParams = newEntry ? (newEntry.params || []) : [];
        var oldParams = row._params || [];
        var oldKindByKey = ({});
        for (var i = 0; i < oldParams.length; ++i)
            oldKindByKey[oldParams[i].key] = oldParams[i].kind;
        for (var j = 0; j < newParams.length; ++j) {
            var newParam = newParams[j];
            // Only migrate when the previous descriptor agreed on both the
            // key AND the kind — anything looser risks slotting a value
            // typed for one picker into a slot for another (e.g. dropping
            // a layoutId into a tilingAlgorithm field).
            if (oldKindByKey[newParam.key] === newParam.kind && row.action[newParam.key] !== undefined)
                payload[newParam.key] = row.action[newParam.key];
        }
        return payload;
    }

    /// The actionTypeOptions descriptor whose `value` equals @p type.
    function _entryForType(type) {
        for (var i = 0; i < row.actionTypeOptions.length; ++i) {
            if (row.actionTypeOptions[i].value === type)
                return row.actionTypeOptions[i];
        }
        return undefined;
    }

    spacing: Kirigami.Units.smallSpacing

    // Reset session locks when the user switches the effect. The shared
    // ShaderParamsEditor owns the working-state lock map, so the reset
    // clears it on the loaded editor instance. Tracking via a Connections
    // handler so the reset fires every time the effectId transitions (not
    // just on the initial set). `target` is declared BEFORE the
    // signal-handler function — strict QML resolves signal names against
    // the target's metaobject at parse time.
    Connections {
        function onActionEdited(updated) {
            // Compare against the action BEFORE the edit lands — `row.action`
            // is still the previous state until the parent re-feeds us.
            if ((row.action.type === "overrideAnimationShader" || row.action.type === "overrideOverlayShader") && updated && updated.effectId !== row.action.effectId && shaderParamsLoader.item)
                shaderParamsLoader.item.lockedParams = ({});
        }

        target: row
    }

    // ── Top: action-type picker + per-param editors + delete ─────────────
    RowLayout {
        // The action-type picker is `PZCommon.CategoryMenuButton` (a cascading
        // category menu, declared further below); it gets a fixed
        // `Layout.preferredWidth` wide enough for the longest action label.

        Layout.fillWidth: true
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Icon {
            source: "arrow-right"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.alignment: Qt.AlignVCenter
        }

        // Categorized action-type picker — the shared cascading category-menu
        // button (PZCommon.CategoryMenuButton). Grouped into Gaps / Engine /
        // Snapping / Tiling / Overlay / Animation / Appearance / Window, with
        // Tiling nesting Algorithm and Behavior submenus. Context-domain actions
        // that can't fire against a window-property match render dimmed with a
        // warning tooltip (the picker's `dimmed` item flag); the per-row chip
        // below + the sheet's InlineMessage reinforce it for an action that's
        // already selected.
        PZCommon.CategoryMenuButton {
            id: typeCombo

            // Wide enough for the longest action label ("Override animation
            // duration") so the closed picker never elides its current value.
            Layout.preferredWidth: Kirigami.Units.gridUnit * 13
            // Map the action metadata to the picker's item shape. A context-
            // domain action against a non-context-only match never fires, so
            // mark it dimmed (with a warning tooltip) — the binding re-reads
            // `matchIsContextOnly`, so the dim state follows match edits.
            items: row.actionTypeOptions.map(function (o) {
                var incompatible = o.domain === "context" && !row.matchIsContextOnly;
                return {
                    "id": o.value,
                    "name": o.label,
                    "category": o.category,
                    "categoryOrder": o.categoryOrder,
                    "categoryGroup": o.categoryGroup,
                    "dimmed": incompatible,
                    "dimReason": incompatible ? i18n("This action runs during context resolution and cannot match window properties. Remove the window conditions from the rule's match, or pick a different action.") : ""
                };
            })
            currentId: row.action.type
            placeholderText: i18n("Choose…")
            Accessible.description: i18n("Action type")
            onSelected: function (value) {
                // Type-switch must seed the new param set's defaults — emitting
                // a bare `{ type: newType }` left every parameter undefined,
                // which a SpinBox renders as 0 and `canSave` then gates the
                // rule on. Route through the controller's `defaultPayloadFor`
                // (via row._payloadForType) so the same kind→default mapping
                // that drives ActionListEditor._append also drives a type
                // change here.
                if (value === row.action.type)
                    return;

                row.actionEdited(row._payloadForType(value));
            }
        }

        // Per-row warning chip — surfaces when the action's current type is
        // incompatible with the rule's match. The full message lives in the
        // RuleEditorSheet's InlineMessage (which lists every issue across
        // every action); this chip pins the warning to the offending row so
        // the user can spot which action to change in a multi-action rule.
        Kirigami.Icon {
            visible: row._currentTypeIncompatible
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            source: "dialog-warning"
            Accessible.name: i18n("Action incompatible with the rule's match")
            ToolTip.visible: incompatibleHover.hovered
            ToolTip.delay: Kirigami.Units.toolTipDelay
            ToolTip.text: i18n("This action runs during context resolution, but the rule's match references window properties, so the action would never fire as written.")

            HoverHandler {
                id: incompatibleHover
            }
        }

        // Stock-animation conflict chip — a per-window shader on the minimize
        // or maximize event cannot suppress KDE's own effect for that event.
        // Predicate, tooltip, and the tree-suppression hide condition all
        // live in the shared component (also used by the read-only rule
        // summary, ActionListView).
        StockAnimationConflictChip {
            action: row.action
            animationsController: row.appSettings ? row.appSettings.animationsController : null
        }

        // One editor per parameter — the shape comes from the param `kind`,
        // never an action-type ladder. The param-editor Components live on the
        // `paramEditors` handle (ActionParamEditors.qml, instantiated above) so
        // the Loader inside the Repeater delegate can resolve them via
        // `paramEditors._…ParamEditor`. Plain `id`-based sibling references don't
        // propagate into a Repeater delegate's QML scope, which is why
        // MatchLeafEditor (a root-level Loader) works but this pattern
        // previously rendered blank.
        Repeater {
            model: row._params

            delegate: Loader {
                required property var modelData

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                sourceComponent: {
                    if (modelData.kind === "enum")
                        return paramEditors._enumParamEditor;

                    if (modelData.kind === "number" || modelData.kind === "percent")
                        return paramEditors._numberParamEditor;

                    if (modelData.kind === "snappingLayout")
                        return paramEditors._snappingLayoutEditor;

                    if (modelData.kind === "tilingAlgorithm")
                        return paramEditors._tilingAlgorithmEditor;

                    if (modelData.kind === "animationEvent")
                        return paramEditors._animationEventEditor;

                    if (modelData.kind === "shaderEffect")
                        return paramEditors._shaderEffectEditor;

                    if (modelData.kind === "decorationChain")
                        return paramEditors._decorationChainEditor;

                    if (modelData.kind === "overlayShader")
                        return paramEditors._overlayShaderEditor;

                    if (modelData.kind === "curveEditor")
                        return paramEditors._curveEditorEditor;

                    if (modelData.kind === "bool")
                        return paramEditors._boolParamEditor;

                    if (modelData.kind === "color")
                        return paramEditors._colorParamEditor;

                    if (modelData.kind === "zoneOrdinals")
                        return paramEditors._zoneOrdinalsEditor;

                    if (modelData.kind === "screenId")
                        return paramEditors._screenIdEditor;

                    if (modelData.kind === "virtualDesktop")
                        return paramEditors._virtualDesktopEditor;

                    return paramEditors._stringParamEditor;
                }
            }
        }

        ToolButton {
            icon.name: "edit-delete"
            Layout.alignment: Qt.AlignVCenter
            ToolTip.text: i18n("Remove action")
            ToolTip.visible: hovered
            Accessible.name: i18n("Remove this action")
            onClicked: row.removeRequested()
        }
    }

    // ── Input-format hint for the current params ─────────────────────────────
    // A muted helper line under the editor row, shown only when a param carries
    // a `hint` (e.g. SnapToZone's zone-ordinal syntax). Indented to align under
    // the editors, mirroring the shader-parameter editor's left margin. Plain
    // text, word-wrapped; never interactive.
    Label {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
        visible: row._paramHint.length > 0
        text: row._paramHint
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
        Accessible.ignored: true
    }

    // ── Bottom: shader-parameter editor for the shader-override actions ──────
    // Surfaces when the action type is `overrideAnimationShader` or
    // `overrideOverlayShader`, the user has picked an effect, and that effect
    // declares parameters (`_activeShaderParamSchema` resolves the right
    // registry's schema). Matches the per-event editor on the animation settings
    // page so users can tweak uniforms without leaving the rule editor.
    Loader {
        id: shaderParamsLoader

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
        active: row._activeShaderParamSchema.length > 0
        visible: active
        sourceComponent: paramEditors._shaderParamsEditor
    }

    // ── Bottom: decoration-chain rows for OverrideDecorationChain ────────────
    // The inline slot holds only the compact add-picker; the chain itself
    // (ordered rows with reorder/remove and inline per-pack parameters)
    // renders full-width down here, mirroring the decoration surface cards.
    Loader {
        id: decorationChainLoader

        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
        active: row.action.type === "overrideDecorationChain"
        visible: active
        sourceComponent: paramEditors._decorationChainListEditor
    }

    // ── Bottom: custom-parameter editor for SetAlgorithmParam ────────────────
    // Surfaces when the action is `setAlgorithmParam`, the user has picked an
    // algorithm, and that algorithm declares custom params. Reuses the shared
    // ParameterEditor (as the shader-params editor above does); the algorithm
    // schema is adapted to the descriptor shape by row._adaptedAlgorithmParamSchema.
    Loader {
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
        active: row._activeAlgorithmParamSchema.length > 0
        visible: active
        sourceComponent: paramEditors._algorithmParamsEditor
    }
}
