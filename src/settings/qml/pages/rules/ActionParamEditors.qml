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
 * @brief The per-parameter editor Components for ActionRow, hoisted out of it.
 *
 * ActionRow's `_params` Repeater/Loader picks one of these `Component`s by the
 * param `kind` (never an action-type ladder). They live here rather than inline
 * so ActionRow stays under the file-size ceiling; the split is purely a home for
 * the Components, not a behavioural boundary.
 *
 * Each Component body is UNCHANGED from when it lived in ActionRow: it still
 * reads `row.*` (the ActionRow instance, injected below) and `parent.modelData`
 * (the hosting Loader's param descriptor, reached at runtime). ActionRow keeps
 * owning the state (`action`, `appSettings`, `_withParam`, the schema/key
 * helpers); this object owns only the editor templates.
 */
QtObject {
    id: editors

    /// The ActionRow these editors belong to. Named `row` so the moved Component
    /// bodies resolve `row.action` / `row.actionEdited` / `row._withParam`
    /// against it exactly as they did inline, with no rewrites.
    required property var row

    // Param-editor Components — the `modelData` they reference is the
    // **Loader**'s `modelData` (set by Repeater), reached via `parent.modelData`
    // since each loaded item is parented to the Loader. A `required property
    // var modelData` on the inner control wouldn't be initialised — Loader
    // doesn't auto-forward its own modelData into the loaded item's scope.
    property Component _stringParamEditor: Component {
        TextField {
            readonly property var _param: parent.modelData

            text: row.action[_param.key] !== undefined ? String(row.action[_param.key]) : ""
            placeholderText: _param.label
            Accessible.name: _param.label
            onEditingFinished: row.actionEdited(row._withParam(_param.key, text))
        }
    }

    // Comma/space/range-separated zone-number input for `kind == "zoneOrdinals"`
    // (SnapToZone). Stores a JSON array of 1-based ordinals; multiple ordinals
    // span their combined area. Accepts "1, 2", "1;2", "1 2", and ranges "1-3".
    property Component _zoneOrdinalsEditor: Component {
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

    property Component _numberParamEditor: Component {
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

    // Toggle editor for `kind == "bool"` params (e.g. SetHideTitleBar /
    // SetBorderVisible / SetUsePerSideOuterGap / RestorePosition — dispatch is by
    // `kind`, not this list). Stores a JSON bool.
    property Component _boolParamEditor: Component {
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
                // Caption the toggle with the action's polarity-aware phrase
                // ("Show border" / "Hide border", "Lock layout" / "Don't lock
                // layout") so its current effect is legible, not just whether it
                // is on. The wording comes from the param descriptor's
                // onLabel/offLabel (RuleAuthoring::boolActionStateLabel, shared
                // with the rule-list summary); a bool param without a curated
                // phrase falls back to plain On / Off.
                label: checked ? (_param.onLabel !== undefined ? _param.onLabel : i18n("On")) : (_param.offLabel !== undefined ? _param.offLabel : i18n("Off"))
                onToggled: function (newValue) {
                    row.actionEdited(row._withParam(_param.key, newValue));
                }
            }
        }
    }

    // Colour swatch + picker for `kind == "color"` params — the single `value`
    // colour on SetBorderColorActive / SetBorderColorInactive. Stores a
    // `#AARRGGBB` wire string (alpha-first, matching QColor::HexArgb and the
    // global zone/border colours) so transparency set in the picker survives.
    // The validator accepts the `#AARRGGBB` shape and the effect-side consumer
    // parses it via `QColor(QString)` (which reads 9-digit hex alpha-first).
    property Component _colorParamEditor: Component {
        RowLayout {
            readonly property var _param: parent.modelData
            // Final fallback (colour param with no stored value AND no metadata
            // default) derives from the theme's accent rather than a hardcoded
            // hex (CLAUDE.md: never hardcode colors).
            readonly property string _hex: (row.action[_param.key] !== undefined && row.action[_param.key] !== "") ? String(row.action[_param.key]) : (_param.default !== undefined ? String(_param.default) : String(Kirigami.Theme.highlightColor))
            // A border-colour action's single `value` param may carry the "accent"
            // sentinel ("follow the system accent") instead of a hex string. It is
            // not a QColor, so render the live system colour for the swatch and a
            // word for the label rather than letting QColor("accent") fall to black.
            readonly property bool _isAccent: _hex === "accent"
            // The accent sentinel follows the system colour scheme per focus state,
            // the same split updateWindowDecoration applies: the focused (active) slot
            // adopts the highlight colour, the unfocused (inactive) slot the inactive
            // colour. Preview the matching system colour WITH its alpha so the swatch
            // shows what the border will actually draw instead of one opaque accent
            // for both. Falls back to the theme highlight when settings are absent.
            readonly property color _accentColor: !row.appSettings ? Kirigami.Theme.highlightColor : (row.action.type === "setBorderColorInactive" ? row.appSettings.inactiveColor : row.appSettings.highlightColor)

            spacing: Kirigami.Units.smallSpacing

            ColorButton {
                id: swatch

                color: parent._isAccent ? parent._accentColor : parent._hex
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

    property Component _enumParamEditor: Component {
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
    property Component _snappingLayoutEditor: Component {
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
    property Component _screenIdEditor: Component {
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
    property Component _virtualDesktopEditor: Component {
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
    property Component _tilingAlgorithmEditor: Component {
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

    // Animation-event picker — flattens `eventSections()` from the animations
    // controller into a single dropdown of leaf paths (skipping category-only
    // rows). The wire value remains the dotted path (`window.open`).
    property Component _animationEventEditor: Component {
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

    // Curve picker — wraps `CurveEditorDialog`, the same dialog the per-event
    // animation card uses. The stored wire value is a single `curve` string:
    //   - named easing (e.g. `out-cubic`)
    //   - bezier (`cubic-bezier:x1,y1,x2,y2`)
    //   - spring (`spring:omega,zeta`)
    // Parse decides which mode to seed the dialog with; Apply encodes the
    // dialog's working state back into one of those forms.
    property Component _curveEditorEditor: Component {
        Item {
            id: curveSlot

            readonly property var _param: parent.modelData
            readonly property string _stored: row.action[_param.key] !== undefined ? String(row.action[_param.key]) : ""
            readonly property bool _isSpring: _stored.indexOf("spring:") === 0
            // All-or-nothing decode via the shared parser; a non-spring
            // `_stored` yields the engine spring defaults, matching the
            // old per-property guards without a second hand-rolled parse.
            readonly property var _spring: CurvePresets.parseSpring(_stored)
            readonly property real _springOmega: _spring.omega
            readonly property real _springZeta: _spring.zeta
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

    property Component _decorationChainEditor: Component {
        // Inline half of the OverrideDecorationChain editor: a compact
        // add-picker sitting in the THEN row (mirroring the shader-effect
        // picker's placement); selecting a pack APPENDS it to the action's
        // chain. The chain rows themselves render full-width BELOW the row
        // via decorationChainLoader, exactly like the shader-uniform editor
        // splits from its inline effect picker.
        PZCommon.CategoryMenuButton {
            readonly property var _param: parent.modelData
            readonly property var _chain: row.action[row._decorationChainKey] || []
            // Packs not already in the chain; re-evaluates as the chain grows.
            readonly property var _addable: {
                var all = row.appSettings && row.appSettings.decorationPage ? row.appSettings.decorationPage.availableShaderEffects() : [];
                var out = [];
                for (var i = 0; i < all.length; i++) {
                    if (all[i] && all[i].id && _chain.indexOf(all[i].id) < 0)
                        out.push(all[i]);
                }
                return out;
            }

            items: _addable
            enabled: _addable.length > 0
            currentId: ""
            includeNoneEntry: false
            placeholderText: i18nc("@action:button", "Add a pack…")
            Accessible.description: _param.label
            onSelected: function (id) {
                if (!id || id.length === 0)
                    return;
                var next = _chain.slice();
                next.push(id);
                row.actionEdited(row._withParam(row._decorationChainKey, next));
            }
        }
    }

    property Component _decorationChainListEditor: Component {
        // Below-row half of the OverrideDecorationChain editor: the SAME
        // chain rows the decoration surface cards embed (reorder, remove,
        // expand for full descriptions + per-pack parameters), rewired so
        // every signal mutates the ACTION payload. The add row is hidden
        // (the inline picker above owns adding) and so are the layer
        // toggles (a rule chain is explicit; remove a pack instead). An
        // empty chain is the valid "no decoration" sentinel.
        ChainEditor {
            availableShaders: row.appSettings && row.appSettings.decorationPage ? row.appSettings.decorationPage.availableShaderEffects() : []
            chain: row.action[row._decorationChainKey] || []
            // Hoisted stable-empty identity (same as _shaderParamsEditor's
            // currentValues) rather than an inline `({})` that churns a new
            // object per binding evaluation.
            packParameters: row.action[row._decorationParamsKey] || row._emptyShaderParams
            showLayerToggles: false
            showAddRow: false
            onChainChangeRequested: function (newChain) {
                // Prune params of removed packs, mirroring
                // DecorationPageController.setChain: re-adding a pack starts
                // from its defaults rather than resurrecting old overrides.
                var cur = row.action[row._decorationParamsKey] || {};
                var pruned = {};
                for (var pid in cur) {
                    if (newChain.indexOf(pid) >= 0)
                        pruned[pid] = cur[pid];
                }
                var next = row._withParam(row._decorationChainKey, newChain);
                next[row._decorationParamsKey] = pruned;
                row.actionEdited(next);
            }
            onParamChangeRequested: function (packId, paramId, value) {
                var cur = row.action[row._decorationParamsKey] || {};
                var params = {};
                for (var pid in cur)
                    params[pid] = cur[pid];
                var packParams = {};
                var curPack = params[packId] || {};
                for (var k in curPack)
                    packParams[k] = curPack[k];
                packParams[paramId] = value;
                params[packId] = packParams;
                row.actionEdited(row._withParam(row._decorationParamsKey, params));
            }
            onParamsRandomizeRequested: function (packId, rolled) {
                var cur = row.action[row._decorationParamsKey] || {};
                var params = {};
                for (var pid in cur)
                    params[pid] = cur[pid];
                var packParams = {};
                var curPack = params[packId] || {};
                for (var k in curPack)
                    packParams[k] = curPack[k];
                for (var r in rolled)
                    packParams[r] = rolled[r];
                params[packId] = packParams;
                row.actionEdited(row._withParam(row._decorationParamsKey, params));
            }
            onParamsResetRequested: function (packId, defaults) {
                var cur = row.action[row._decorationParamsKey] || {};
                var params = {};
                for (var pid in cur)
                    params[pid] = cur[pid];
                var packParams = {};
                var curPack = params[packId] || {};
                for (var k in curPack)
                    packParams[k] = curPack[k];
                for (var d in defaults)
                    packParams[d] = defaults[d];
                params[packId] = packParams;
                row.actionEdited(row._withParam(row._decorationParamsKey, params));
            }
        }
    }

    // Shader-effect picker — a cascading category menu fed by the path-aware
    // `availableShaderEffectsForPath(event)`, so shaders group by category and
    // ones incompatible with the action's target event render dimmed. Wire
    // value is the effect id.
    property Component _shaderEffectEditor: Component {
        // Cascading category menu (same widget as the action-type picker above
        // and the animations page's shader picker) instead of a flat combo, so
        // shaders group by category. The list is path-aware: it is pre-filtered
        // to the shaders that can drive this action's target event, so a
        // geometry-only shader (window-morph) is omitted on a show/hide event,
        // matching the animations page and the WHEN/THEN pickers in this same
        // editor.
        PZCommon.CategoryMenuButton {
            readonly property var _param: parent.modelData
            // Reading `row.action.event` re-filters the list when the user
            // changes the action's target event. Empty event → the controller
            // leaves every shader compatible (nothing to filter yet).
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

    // Overlay-shader picker for OverrideOverlayShader actions — the overlay/
    // snapping shader registry (Snapping → Shaders page), distinct from the
    // animation shaders above. Wire value is the shader id.
    property Component _overlayShaderEditor: Component {
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
    property Component _shaderParamsEditor: Component {
        PZCommon.ShaderParamsEditor {
            id: paramEditor

            parameters: row._activeShaderParamSchema
            currentValues: row.action.params || row._emptyShaderParams
            effectId: row.action.effectId || ""
            enableLocking: true
            enableRandomize: true
            enableImage: false
            compact: true
            // The shared editor owns the session-only lock map and hosts the
            // colour dialog; the rule only persists values. Locks reset on
            // effect switch via the Loader (see the Connections handler above).
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
                // Full defaults map replaces the rule's param overrides in
                // one write, mirroring the randomize batch above.
                row.actionEdited(row._withParam("params", defaults));
            }
        }
    }

    // Inline custom-parameter editor for SetAlgorithmParam — a Repeater over the
    // algorithm's Luau-declared param schema (`_activeAlgorithmParamSchema`),
    // rendering a slider / switch / combo per param type and writing each value
    // into the action's nested `params` object via `_writeAlgorithmParam`. The
    // autotile analogue of `_shaderParamsEditor`; mirrors the per-algorithm
    // editor on the tiling settings page (TilingAlgorithmPage.qml).
    property Component _algorithmParamsEditor: Component {
        // The shared parameter editor, same as the shader / decoration-pack
        // param UIs, minus the lock / randomize affordances. The algorithm
        // schema is adapted to the descriptor shape via row._adapted*.
        PZCommon.ParameterEditor {
            id: algorithmParamEditor

            Layout.fillWidth: true
            parameters: row._adaptedAlgorithmParamSchema
            currentValues: row._algorithmParamValues
            compact: true
            enableLocking: false
            enableRandomize: false
            // No lock/randomize for algorithm params, but reset-to-default is
            // useful on its own — clears the rule's overrides back to the
            // algorithm's declared defaults.
            enableReset: true
            enableImage: false
            enableGroups: false
            showParametersHeader: true
            onValueChanged: function (paramId, value) {
                row._writeAlgorithmParam(paramId, value);
            }
            onResetRequested: {
                // Replace the override map with every param's default in one
                // write (batch, like the shader action's reset).
                row.actionEdited(row._withParam("params", algorithmParamEditor.computeDefaults()));
            }
        }
    }
}
