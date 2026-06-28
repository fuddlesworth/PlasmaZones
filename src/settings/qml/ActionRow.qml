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
 * `ShaderParameterEditor` surfaces below the row when an effect is selected so
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
    /// ShaderParameterEditor below the row.
    readonly property var _activeShaderParamSchema: {
        if (row.action.type === "overrideAnimationShader")
            return row._shaderParamSchema;
        if (row.action.type === "overrideOverlayShader")
            return row._overlayShaderParamSchema;
        return [];
    }
    /// Working-state lock map for the shader-params editor — mirrors the
    /// per-event animation editor's behaviour. Locks influence randomize
    /// (locked params are kept) but are NOT persisted to the rule, so a
    /// reopened rule starts with no locks (same as the per-event card).
    /// The map is reset whenever the selected `effectId` changes so locks
    /// for the previous effect don't leak into the new effect's params
    /// (e.g. `intensity` locked under Smoke shouldn't carry into BMW).
    property var _shaderParamLocks: ({})
    /// Stable empty-object fallback for the inline shader params editor's
    /// `currentValues` binding — using `({})` inline would allocate a new
    /// object identity per binding evaluation and churn the editor.
    readonly property var _emptyShaderParams: ({})
    // Param-editor Components — the `modelData` they reference is the
    // **Loader**'s `modelData` (set by Repeater), reached via `parent.modelData`
    // since each loaded item is parented to the Loader. A `required property
    // var modelData` on the inner control wouldn't be initialised — Loader
    // doesn't auto-forward its own modelData into the loaded item's scope.
    property Component _stringParamEditor
    property Component _numberParamEditor
    property Component _enumParamEditor
    // Rich preview pickers — `LayoutComboBox` shows a mini zone preview +
    // category / aspect badges per entry, same component the assignment
    // pages use. LayoutComboBox now follows the WideComboBox stacking
    // pattern (reparented popup + outside-click catcher) so it works
    // inside the OverlaySheet host without being out-z-ordered or
    // triggering a focus-loss auto-close.
    property Component _snappingLayoutEditor
    property Component _tilingAlgorithmEditor
    // Animation-event picker — flattens `eventSections()` from the animations
    // controller into a single dropdown of leaf paths (skipping category-only
    // rows). The wire value remains the dotted path (`window.open`).
    property Component _animationEventEditor
    // Curve picker — wraps `CurveEditorDialog`, the same dialog the per-event
    // animation card uses. The stored wire value is a single `curve` string:
    //   - named easing (e.g. `out-cubic`)
    //   - bezier (`cubic-bezier:x1,y1,x2,y2`)
    //   - spring (`spring:omega,zeta`)
    // Parse decides which mode to seed the dialog with; Apply encodes the
    // dialog's working state back into one of those forms.
    property Component _curveEditorEditor
    // Shader-effect picker — a cascading category menu fed by the path-aware
    // `availableShaderEffectsForPath(event)`, so shaders group by category and
    // ones incompatible with the action's target event render dimmed. Wire
    // value is the effect id.
    property Component _shaderEffectEditor
    // Overlay-shader picker for OverrideOverlayShader actions — the overlay/
    // snapping shader registry (Snapping → Shaders page), distinct from the
    // animation shaders above. Wire value is the shader id.
    property Component _overlayShaderEditor
    // Inline shader-uniform editor shared by both shader-override actions
    // (OverrideAnimationShader and OverrideOverlayShader) — bound to
    // `_activeShaderParamSchema`, which selects the matching registry's schema
    // per action type. The action stores a nested `params` object (the shader
    // uniform values);
    // changing any value rewrites the whole object. Locks live on the row
    // as working state (not persisted) — exactly like the per-event card on
    // the animations page. Randomize rolls a new map respecting locks and
    // writes it back to `action.params`. Image picking is disabled because
    // shader-image uniforms aren't part of the rule wire format here.
    property Component _shaderParamsEditor
    // Toggle editor for `kind == "bool"` params (e.g. SetHideTitleBar /
    // SetBorderVisible / SetUsePerSideOuterGap / RestorePosition — dispatch is by
    // `kind`, not this list). Stores a JSON bool.
    property Component _boolParamEditor
    // Colour swatch + picker for `kind == "color"` params — the single `value`
    // colour on SetBorderColorActive / SetBorderColorInactive. Stores a
    // `#AARRGGBB` wire string (alpha-first, matching QColor::HexArgb and the
    // global zone/border colours) so transparency set in the picker survives.
    // The validator accepts the `#AARRGGBB` shape and the effect-side consumer
    // parses it via `QColor(QString)` (which reads 9-digit hex alpha-first).
    property Component _colorParamEditor
    // Comma/space/range-separated zone-number input for `kind == "zoneOrdinals"`
    // (SnapToZone). Stores a JSON array of 1-based ordinals; multiple ordinals
    // span their combined area. Accepts "1, 2", "1;2", "1 2", and ranges "1-3".
    property Component _zoneOrdinalsEditor
    // Monitor picker for `kind == "screenId"` (RouteToScreen). Drives off
    // `appSettings.screens`; the wire value is the canonical screen id.
    property Component _screenIdEditor
    // Virtual-desktop picker for `kind == "virtualDesktop"` (RouteToDesktop).
    // Lists 1..virtualDesktopCount; the wire value is the 1-based desktop number.
    property Component _virtualDesktopEditor

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

    // Reset session locks when the user switches the effect. Tracking via
    // a Connections handler so the reset fires every time the effectId
    // transitions (not just on the initial set). `target` is declared
    // BEFORE the signal-handler function — strict QML resolves signal
    // names against the target's metaobject at parse time.
    Connections {
        function onActionEdited(updated) {
            // Compare against the action BEFORE the edit lands — `row.action`
            // is still the previous state until the parent re-feeds us.
            if ((row.action.type === "overrideAnimationShader" || row.action.type === "overrideOverlayShader") && updated && updated.effectId !== row.action.effectId)
                row._shaderParamLocks = ({});
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
        // button (PZCommon.CategoryMenuButton). Grouped into Layout & engine /
        // Gaps / Window / Appearance / Animation. Context-domain actions that
        // can't fire against a window-property match render dimmed with a
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

        // One editor per parameter — the shape comes from the param `kind`,
        // never an action-type ladder. The param-editor Components are
        // declared as `property Component` on `row` (below) so the Loader
        // inside the Repeater delegate can resolve them via
        // `row._…ParamEditor`. Plain `id`-based sibling references don't
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
                        return row._enumParamEditor;

                    if (modelData.kind === "number" || modelData.kind === "percent")
                        return row._numberParamEditor;

                    if (modelData.kind === "snappingLayout")
                        return row._snappingLayoutEditor;

                    if (modelData.kind === "tilingAlgorithm")
                        return row._tilingAlgorithmEditor;

                    if (modelData.kind === "animationEvent")
                        return row._animationEventEditor;

                    if (modelData.kind === "shaderEffect")
                        return row._shaderEffectEditor;

                    if (modelData.kind === "overlayShader")
                        return row._overlayShaderEditor;

                    if (modelData.kind === "curveEditor")
                        return row._curveEditorEditor;

                    if (modelData.kind === "bool")
                        return row._boolParamEditor;

                    if (modelData.kind === "color")
                        return row._colorParamEditor;

                    if (modelData.kind === "zoneOrdinals")
                        return row._zoneOrdinalsEditor;

                    if (modelData.kind === "screenId")
                        return row._screenIdEditor;

                    if (modelData.kind === "virtualDesktop")
                        return row._virtualDesktopEditor;

                    return row._stringParamEditor;
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
        Layout.fillWidth: true
        Layout.leftMargin: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
        active: row._activeShaderParamSchema.length > 0
        visible: active
        sourceComponent: row._shaderParamsEditor
    }

    _stringParamEditor: Component {
        TextField {
            readonly property var _param: parent.modelData

            text: row.action[_param.key] !== undefined ? String(row.action[_param.key]) : ""
            placeholderText: _param.label
            Accessible.name: _param.label
            onEditingFinished: row.actionEdited(row._withParam(_param.key, text))
        }
    }

    _zoneOrdinalsEditor: Component {
        TextField {
            readonly property var _param: parent.modelData
            readonly property var _zones: Array.isArray(row.action[_param.key]) ? row.action[_param.key] : []

            // Normalised display (sorted, deduped) re-binds after each edit.
            text: _zones.join(", ")
            placeholderText: i18nc("@info:placeholder zone numbers for a snap-to-zone rule", "e.g. 1, 2 or 1-2")
            Accessible.name: _param.label
            Accessible.description: i18nc("@info:whatsthis", "One or more 1-based zone numbers to snap matched windows to. Multiple zones span their combined area.")
            onEditingFinished: {
                // Parse comma/semicolon/space-separated ordinals and "lo-hi"
                // ranges into a deduped, ascending array of 1-based integers.
                var seen = ({});
                var parsed = [];
                var tokens = text.split(/[,;\s]+/);
                for (var i = 0; i < tokens.length; i++) {
                    var t = tokens[i].trim();
                    if (t.length === 0)
                        continue;
                    var range = t.match(/^(\d+)-(\d+)$/);
                    if (range) {
                        var lo = parseInt(range[1], 10);
                        var hi = parseInt(range[2], 10);
                        // Clamp the upper bound to the SnapToZone ordinal cap
                        // (MaxZoneOrdinal = 64 in RuleAction.h). An unbounded
                        // expansion (e.g. "1-100000") would build a huge array on
                        // the UI thread and freeze it; ordinals past the cap are
                        // rejected by the validator anyway.
                        if (hi > 64)
                            hi = 64;
                        if (lo >= 1 && hi >= lo) {
                            for (var z = lo; z <= hi; z++) {
                                if (!seen[z]) {
                                    seen[z] = true;
                                    parsed.push(z);
                                }
                            }
                        }
                        continue;
                    }
                    if (/^\d+$/.test(t)) {
                        var n = parseInt(t, 10);
                        if (n >= 1 && !seen[n]) {
                            seen[n] = true;
                            parsed.push(n);
                        }
                    }
                }
                parsed.sort(function (a, b) {
                    return a - b;
                });
                // A SnapToZone action requires a non-empty ordinal list (the
                // descriptor validator rejects []). If the user cleared the field
                // or typed only invalid tokens, keep the last valid value rather
                // than committing an empty list — that would produce an action the
                // validator drops on save, silently losing the rule with the Save
                // button still enabled. Restore via Qt.binding (not a bare
                // `text = ...`) so the declarative `text: _zones.join(", ")`
                // binding survives and keeps normalising on later edits.
                if (parsed.length === 0) {
                    text = Qt.binding(function () {
                        return _zones.join(", ");
                    });
                    return;
                }
                row.actionEdited(row._withParam(_param.key, parsed));
            }
        }
    }

    _numberParamEditor: Component {
        SpinBox {
            readonly property var _param: parent.modelData
            // "percent" stores `display * scale`; "number" stores the raw value.
            readonly property real _scale: _param.scale !== undefined ? _param.scale : 1
            readonly property real _stored: row.action[_param.key] !== undefined ? row.action[_param.key] : 0

            from: _param.min !== undefined ? _param.min : 0
            to: _param.max !== undefined ? _param.max : 999999
            value: Math.round(_stored / _scale)
            Accessible.name: _param.label
            onValueModified: row.actionEdited(row._withParam(_param.key, value * _scale))
        }
    }

    _boolParamEditor: Component {
        // SettingsSwitch is a fixed-size custom Item whose track fills its
        // bounds, but the hosting Loader is `Layout.fillWidth` (for the text /
        // combo editors), which would stretch the toggle into a full-width pill.
        // Host it in a fill wrapper and keep the toggle at its implicit size,
        // left- and vertically-centered — matching the compact placement the
        // stock Switch produced.
        Item {
            readonly property var _param: parent.modelData

            implicitHeight: boolToggle.implicitHeight

            SettingsSwitch {
                id: boolToggle

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                checked: row.action[_param.key] === true
                accessibleName: _param.label
                onToggled: function (newValue) {
                    row.actionEdited(row._withParam(_param.key, newValue));
                }
            }
        }
    }

    _colorParamEditor: Component {
        RowLayout {
            readonly property var _param: parent.modelData
            readonly property string _hex: (row.action[_param.key] !== undefined && row.action[_param.key] !== "") ? String(row.action[_param.key]) : "#FF3DAEE9"
            // A border-colour action's single `value` param may carry the "accent"
            // sentinel ("follow the system accent") instead of a hex string. It is
            // not a QColor, so render the live accent colour for the swatch and a
            // word for the label rather than letting QColor("accent") fall to black.
            readonly property bool _isAccent: _hex === "accent"

            spacing: Kirigami.Units.smallSpacing

            ColorButton {
                id: swatch

                color: parent._isAccent ? Kirigami.Theme.highlightColor : parent._hex
                Accessible.name: _param.label
                onClicked: colorDialog.open()
            }

            Label {
                // Show the stored #AARRGGBB wire value (alpha-first), or "Accent"
                // for the system-accent sentinel.
                text: parent._isAccent ? i18n("Accent") : parent._hex.toUpperCase()
                font: Kirigami.Theme.fixedWidthFont
            }

            Item {
                Layout.fillWidth: true
            }

            ColorDialog {
                id: colorDialog

                options: ColorDialog.ShowAlphaChannel
                selectedColor: swatch.color
                onAccepted: row.actionEdited(row._withParam(_param.key, row._toHexArgb(selectedColor)))
            }
        }
    }

    _enumParamEditor: Component {
        WideComboBox {
            readonly property var _param: parent.modelData
            // Enum options carry `{value, label}` pairs — `value` is the wire
            // token stored in the rule, `label` is the properly-cased UI
            // string. `textRole` / `valueRole` make the ComboBox display the
            // label while `currentValue` reads back the underlying wire token.
            readonly property var _options: _param.options || []

            model: _options
            textRole: "label"
            valueRole: "value"
            currentIndex: {
                var target = row.action[_param.key];
                for (var i = 0; i < _options.length; ++i) {
                    if (_options[i].value === target)
                        return i;
                }
                return -1;
            }
            Accessible.name: _param.label
            onActivated: function (index) {
                row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // Both action editors use the rich `LayoutComboBox` — preview tile +
    // category / aspect badges, same component the assignment pages use.
    // `layoutFilter` separates the two streams: 0 = manual / snapping
    // layouts, 1 = autotile algorithms. `showNoneOption: false` because
    // the action either targets a layout or has no value (no implicit
    // "Default" fallback inside a rule).
    _snappingLayoutEditor: Component {
        LayoutComboBox {
            readonly property var _param: parent.modelData

            Accessible.name: _param.label
            appSettings: row.appSettings
            currentLayoutId: row.action[_param.key] || ""
            layoutFilter: 0
            showNoneOption: false
            showPreview: true
            onActivated: function (index) {
                row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // Monitor picker for RouteToScreen. Mirrors the ScreenId match-condition
    // editor (MatchLeafEditor's screenValueEditor): the user picks a friendly
    // label while the wire value stays the canonical screen id. A stored id whose
    // monitor is offline still surfaces (so the user sees what the rule pins to).
    _screenIdEditor: Component {
        WideComboBox {
            id: screenCombo

            readonly property var _param: parent.modelData
            readonly property var _screens: row.appSettings ? row.appSettings.screens : []
            model: _screens.map(function (s) {
                var label = s.displayLabel || s.name || "";
                if (s.isPrimary)
                    label += " · " + i18n("Primary");
                return {
                    "label": label,
                    "name": s.name
                };
            })
            textRole: "label"
            valueRole: "name"
            currentIndex: {
                var target = row.action[_param.key] || "";
                for (var i = 0; i < screenCombo._screens.length; ++i) {
                    if (screenCombo._screens[i].name === target)
                        return i;
                }
                return -1;
            }
            displayText: currentIndex >= 0 ? currentText : (row.action[_param.key] || i18n("Choose a monitor…"))
            Accessible.name: _param.label
            onActivated: function (index) {
                if (currentValue !== row.action[_param.key])
                    row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // Virtual-desktop picker for RouteToDesktop. Lists 1..virtualDesktopCount,
    // labelled with the desktop name when KWin reports one. The wire value is the
    // 1-based desktop number. A stored desktop beyond the current count still
    // surfaces its number so the rule's target stays legible.
    _virtualDesktopEditor: Component {
        WideComboBox {
            id: desktopCombo

            readonly property var _param: parent.modelData
            readonly property int _count: row.appSettings && row.appSettings.virtualDesktopCount > 0 ? row.appSettings.virtualDesktopCount : 1
            readonly property var _names: row.appSettings ? row.appSettings.virtualDesktopNames : []
            model: {
                var items = [];
                for (var i = 1; i <= desktopCombo._count; ++i) {
                    var name = desktopCombo._names.length >= i ? desktopCombo._names[i - 1] : "";
                    items.push({
                        "label": name && name.length > 0 ? (i + ": " + name) : ("" + i),
                        "value": i
                    });
                }
                return items;
            }
            textRole: "label"
            valueRole: "value"
            currentIndex: {
                var target = Number(row.action[_param.key]);
                for (var i = 0; i < desktopCombo.model.length; ++i) {
                    if (desktopCombo.model[i].value === target)
                        return i;
                }
                return -1;
            }
            displayText: currentIndex >= 0 ? currentText : (row.action[_param.key] ? ("" + row.action[_param.key]) : i18n("Choose a desktop…"))
            Accessible.name: _param.label
            onActivated: function (index) {
                if (currentValue !== row.action[_param.key])
                    row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // The rule wire format stores the BARE algorithm registry id (e.g. "bsp"),
    // but LayoutComboBox keys autotile entries by the "autotile:<id>" prefixed
    // form that `appSettings.layouts` ships (shared with every layout picker).
    // Bridge the prefix on the way in — so the saved bare id resolves to a
    // model entry instead of leaving the combo blank — and strip it on the way
    // out — so the rule keeps the bare id the daemon resolves against rather
    // than a prefixed value it can't match. Mirrors the translation in
    // MonitorStatePage / TilingAlgorithmPage / Main.qml.
    _tilingAlgorithmEditor: Component {
        LayoutComboBox {
            readonly property var _param: parent.modelData

            Accessible.name: _param.label
            appSettings: row.appSettings
            currentLayoutId: {
                // Prefix only when not already prefixed: the wire format is the
                // bare id ("bsp"), but a value corrupted by the pre-fix editor
                // (which wrote the combo's "autotile:bsp" verbatim) must round-
                // trip too — and double-prefixing it would leave the combo
                // blank. Matches the list resolver's already-prefixed handling.
                const stored = row.action[_param.key] || "";
                if (stored === "" || stored.startsWith("autotile:"))
                    return stored;
                return "autotile:" + stored;
            }
            layoutFilter: 1
            showNoneOption: false
            showPreview: true
            onActivated: function (index) {
                const bareId = currentValue.startsWith("autotile:") ? currentValue.substring(9) : currentValue;
                row.actionEdited(row._withParam(_param.key, bareId));
            }
        }
    }

    _animationEventEditor: Component {
        // Categorized event picker — the shared cascading category-menu button
        // (PZCommon.CategoryMenuButton), grouping the events by their section
        // (Window / Editor / Overlays / …) into submenus instead of the long
        // flat "Section · Event" combo. The read-only rule row still renders the
        // full "Section · Event" label (see ActionListView._resolveParamValue).
        PZCommon.CategoryMenuButton {
            readonly property var _param: parent.modelData

            // Map eventSections() into the picker's `{ id, name, category,
            // categoryOrder }` shape: one item per leaf event, grouped under its
            // section. `categoryOrder` preserves the section order (Window,
            // Editor, Overlays, …); within a section the component sorts events
            // by name. An unknown/custom stored path renders as "(missing: …)"
            // rather than collapsing the picker, mirroring the prior fallback.
            items: {
                var controller = row.appSettings ? row.appSettings.animationsController : null;
                if (!controller)
                    return [];

                var sections = controller.eventSections() || [];
                var out = [];
                for (var s = 0; s < sections.length; ++s) {
                    var section = sections[s];
                    var paths = section.paths || [];
                    for (var p = 0; p < paths.length; ++p) {
                        var entry = paths[p];
                        // Skip category-only rows — leaf events are the only
                        // ones a rule can pin to.
                        if (entry.isCategory)
                            continue;

                        out.push({
                            "id": entry.path,
                            "name": entry.label,
                            "category": section.label,
                            "categoryOrder": s
                        });
                    }
                }
                return out;
            }
            currentId: row.action[_param.key] || ""
            placeholderText: i18n("Choose an event…")
            Accessible.description: _param.label
            onSelected: function (value) {
                row.actionEdited(row._withParam(_param.key, value));
            }
        }
    }

    _curveEditorEditor: Component {
        Item {
            id: curveSlot

            readonly property var _param: parent.modelData
            readonly property string _stored: row.action[_param.key] !== undefined ? String(row.action[_param.key]) : ""
            readonly property bool _isSpring: _stored.indexOf("spring:") === 0
            readonly property real _springOmega: {
                if (!_isSpring)
                    return CurvePresets.defaultSpringOmega;

                var parts = _stored.substring(7).split(",");
                var w = parseFloat(parts[0]);
                return isFinite(w) ? w : CurvePresets.defaultSpringOmega;
            }
            readonly property real _springZeta: {
                if (!_isSpring)
                    return CurvePresets.defaultSpringZeta;

                var parts = _stored.substring(7).split(",");
                var z = parseFloat(parts[1]);
                return isFinite(z) ? z : CurvePresets.defaultSpringZeta;
            }
            readonly property string _easingCurve: _isSpring ? CurvePresets.defaultEasingCurve : (_stored || CurvePresets.defaultEasingCurve)
            // Shared naming with the rule-list summary — CurvePresets.curveLabel
            // formats the spring case and defers easing to curveDisplayName.
            // Pass the spring wire value through verbatim; otherwise the resolved
            // easing curve (stored, or the default for a not-yet-set action).
            readonly property string _displayName: CurvePresets.curveLabel(_isSpring ? _stored : _easingCurve)

            implicitHeight: curveButton.implicitHeight
            implicitWidth: curveButton.implicitWidth

            Button {
                id: curveButton

                anchors.fill: parent
                text: curveSlot._displayName
                Accessible.name: curveSlot._param.label
                onClicked: curveDialog.open()
            }

            CurveEditorDialog {
                id: curveDialog

                parent: curveSlot.Window.window ? curveSlot.Window.window.contentItem : curveSlot
                eventLabel: row.action.event || ""
                timingMode: curveSlot._isSpring ? CurvePresets.timingModeSpring : CurvePresets.timingModeEasing
                easingCurve: curveSlot._easingCurve
                springOmega: curveSlot._springOmega
                springZeta: curveSlot._springZeta
                onCurveApplied: function (curve) {
                    row.actionEdited(row._withParam(curveSlot._param.key, curve));
                }
                onSpringApplied: function (omega, zeta) {
                    var encoded = "spring:" + omega.toFixed(2) + "," + zeta.toFixed(2);
                    row.actionEdited(row._withParam(curveSlot._param.key, encoded));
                }
            }
        }
    }

    _shaderEffectEditor: Component {
        // Cascading category menu (same widget as the action-type picker above
        // and the animations page's shader picker) instead of a flat combo, so
        // shaders group by category. The list is path-aware: each effect
        // carries `dimmed`/`dimReason` for this action's target event, so a
        // geometry-only shader (window-morph) is greyed out with a warning
        // tooltip on a show/hide event — matching the animations page and the
        // WHEN/THEN pickers in this same editor.
        PZCommon.CategoryMenuButton {
            readonly property var _param: parent.modelData
            // Reading `row.action.event` makes the dim state re-evaluate when
            // the user changes the action's target event. Empty event → the
            // controller leaves every shader compatible (nothing to dim yet).
            readonly property var _effects: {
                var controller = row.appSettings ? row.appSettings.animationsController : null;
                if (!controller)
                    return [];

                return controller.availableShaderEffectsForPath(row.action.event || "");
            }

            items: _effects
            currentId: row.action[_param.key] || ""
            // CategoryMenuButton renders "(missing: <id>)" on its own for a
            // stale / uninstalled shader id, so the placeholder is only seen
            // when nothing is selected.
            placeholderText: i18n("Choose a shader…")
            Accessible.description: _param.label
            onSelected: function (id) {
                row.actionEdited(row._withParam(_param.key, id));
            }
        }
    }

    _overlayShaderEditor: Component {
        // Cascading category menu of the overlay/snapping shaders — the same
        // registry the "Snapping → Shaders" page edits and that Layout::shaderId
        // stores — grouped by category. Distinct from _shaderEffectEditor, which
        // lists the ANIMATION shaders. No path-aware dim/incompatible state here
        // (overlay shaders are event-agnostic, unlike the per-event animation
        // shaders). Wire value is the shader id; an unknown/uninstalled id
        // renders as "(missing: <id>)".
        PZCommon.CategoryMenuButton {
            readonly property var _param: parent.modelData

            items: {
                var controller = row.appSettings ? row.appSettings.snappingShadersPage : null;
                return controller ? controller.availableShaderEffects() : [];
            }
            currentId: row.action[_param.key] || ""
            placeholderText: i18n("Choose an overlay shader…")
            Accessible.description: _param.label
            onSelected: function (id) {
                row.actionEdited(row._withParam(_param.key, id));
            }
        }
    }

    _shaderParamsEditor: Component {
        PZCommon.ShaderParameterEditor {
            id: paramEditor

            parameters: row._activeShaderParamSchema
            currentValues: row.action.params || row._emptyShaderParams
            lockedParams: row._shaderParamLocks
            enableLocking: true
            enableRandomize: true
            enableGroups: true
            enableImage: false
            compact: true
            onValueChanged: function (paramId, value) {
                // Clone the current param map and stamp the new value so the
                // binding re-evaluates (mutating in place wouldn't trigger).
                var next = ({});
                var existing = row.action.params || ({});
                for (var k in existing)
                    next[k] = existing[k];
                next[paramId] = value;
                row.actionEdited(row._withParam("params", next));
            }
            onLockToggled: function (paramId, locked) {
                // Mirror AnimationProfileEditor — the editor's helper computes
                // the post-toggle map so the same merge logic lives in one
                // place. Locks are session state, not persisted to the rule.
                row._shaderParamLocks = paramEditor.lockedAfterToggle(paramId, locked);
            }
            onLockAllRequested: function (lock) {
                row._shaderParamLocks = paramEditor.lockedAfterAllToggle(lock);
            }
            onRandomizeRequested: {
                // computeRandomized respects locks: locked params keep their
                // current value, the rest are rolled per their schema range.
                var rolled = paramEditor.computeRandomized();
                row.actionEdited(row._withParam("params", rolled));
            }
        }
    }
}
