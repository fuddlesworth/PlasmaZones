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

    /// The reserved explicit-opt-out word (PhosphorZones::NoSnappingLayout /
    /// NoTilingAlgorithm — one spelling, see the AssignmentEntry.h checklist).
    /// Named once here so the two editors that offer the None row share it,
    /// matching MonitorStatePage's `_noLayoutToken` convention.
    readonly property string noLayoutToken: "none"

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

    /// The advisory `max` the descriptor publishes for a SnapToZone param
    /// (the ordinal cap for `zones`, the per-name character cap for
    /// `zoneNames`), read off the row's param schema so neither bound is
    /// re-typed in QML. A schema that carries none means "no cap" rather than
    /// a second copy of the constant.
    function _snapToZoneBound(key) {
        var params = row._params || [];
        for (var i = 0; i < params.length; i++) {
            if (params[i].key === key && params[i].max !== undefined)
                return params[i].max;
        }
        return Number.POSITIVE_INFINITY;
    }

    /// Whether a SnapToZone action payload carries at least one VALID zone
    /// name (non-blank and within the per-name cap after trimming). The
    /// ordinal editor's empty guard consults this so "names only" is a
    /// reachable state; the name editor's twin guard below consults the
    /// ordinal list the same way. Validity, not mere presence: an over-long
    /// name is one the validator rejects, so clearing the ordinals against it
    /// would leave an action the save path refuses.
    function _snapToZoneHasNames(action) {
        var names = action ? action[row._zoneNamesKey] : undefined;
        if (!Array.isArray(names))
            return false;
        var max = editors._snapToZoneBound(row._zoneNamesKey);
        for (var i = 0; i < names.length; i++) {
            if (typeof names[i] !== "string")
                continue;
            var len = names[i].trim().length;
            if (len > 0 && len <= max)
                return true;
        }
        return false;
    }

    /// Whether a SnapToZone action payload carries at least one VALID ordinal
    /// (an integer in 1..max). Validity, not mere presence: a hand-edited
    /// `zones: [0]` is rejected by the validator, so clearing the names against
    /// it would leave an action the save path refuses.
    function _snapToZoneHasOrdinals(action) {
        var zones = action ? action[row._zoneOrdinalsKey] : undefined;
        if (!Array.isArray(zones))
            return false;
        var max = editors._snapToZoneBound(row._zoneOrdinalsKey);
        for (var i = 0; i < zones.length; i++) {
            var n = Number(zones[i]);
            if (Number.isInteger(n) && n >= 1 && n <= max)
                return true;
        }
        return false;
    }

    // Zone-name input for `kind == "zoneNames"` (SnapToZone): a free-text field
    // plus a picker of the names the layouts on disk already use. Parsing and
    // formatting go through the controller (RuleAuthoring::parseZoneNameList /
    // formatZoneNameList) so the field, the picker and the rule-list summary
    // agree on what a name list is: `,` / `;` separated, a name containing a
    // separator rides inside straight double quotes, trimmed, deduped
    // case-insensitively, capped at the descriptor's per-name length. Stores a
    // JSON array of name strings; multiple names span their combined area
    // together with any ordinals.
    property Component _zoneNamesEditor: Component {
        RowLayout {
            id: zoneNamesRow

            readonly property var _param: parent.modelData
            readonly property var _names: Array.isArray(row.action[_param.key]) ? row.action[_param.key] : []
            // Names the picker may still add: the controller's list minus the
            // ones already chosen, compared the way the engine matches them
            // (trimmed, case-insensitive). Mirrors the decoration-chain
            // picker's `_addable`; the button disables when nothing is left.
            readonly property var _addable: {
                var known = row.controller ? row.controller.zoneNames : [];
                var chosen = Object.create(null);
                for (var i = 0; i < _names.length; i++)
                    chosen[String(_names[i]).trim().toLowerCase()] = true;
                var out = [];
                for (var k = 0; k < known.length; k++) {
                    var name = String(known[k]);
                    if (chosen[name.trim().toLowerCase()])
                        continue;
                    out.push({
                        "id": name,
                        "name": name
                    });
                }
                return out;
            }

            spacing: Kirigami.Units.smallSpacing

            function commit(parsed) {
                // A SnapToZone action needs at least one target across its name
                // and ordinal lists. With no valid ordinals, an empty name list
                // would leave an action the validator rejects (Save is gated on
                // it, with a generic footer message), so keep the last value
                // instead, restored via Qt.binding so the declarative text
                // binding survives. With ordinals present, clearing the names is
                // a legitimate "numbers only" target.
                if (parsed.length === 0 && !editors._snapToZoneHasOrdinals(row.action)) {
                    zoneNamesField.text = Qt.binding(function () {
                        return row.controller ? row.controller.formatZoneNameList(zoneNamesRow._names) : zoneNamesRow._names.join(", ");
                    });
                    return;
                }
                row.actionEdited(row._withParam(_param.key, parsed));
            }

            TextField {
                id: zoneNamesField

                Layout.fillWidth: true
                // Normalised display (trimmed, deduped, separator-bearing names
                // quoted) re-binds after each edit.
                text: row.controller ? row.controller.formatZoneNameList(zoneNamesRow._names) : zoneNamesRow._names.join(", ")
                placeholderText: i18nc("@info:placeholder zone names for a snap-to-zone rule", "e.g. Editor, Terminal")
                Accessible.name: zoneNamesRow._param.label
                Accessible.description: i18nc("@info:accessibility zone names field of a snap-to-zone rule", "Zone names to snap matched windows to, found in whichever layout is active. Multiple zones span their combined area.")
                // Array.from: a QStringList crosses into QML as a sequence
                // wrapper, not a JS Array, and the stored payload must be a
                // real array (the guards and the summary test Array.isArray).
                onEditingFinished: zoneNamesRow.commit(row.controller ? Array.from(row.controller.parseZoneNameList(text)) : [])
            }

            // The shared picker rather than a hand-rolled Menu + MenuItem list:
            // CategoryMenuButton is built once and keeps its menu alive across
            // opens, defers the selection through Qt.callLater, and rebuilds
            // lazily when `items` changes while open, which is the documented
            // guard against Qt 6's QQuickMenu teardown race. Picking a name
            // shrinks `_addable`, so the list the user just clicked changes
            // under the click; that is exactly the case the shared component
            // handles.
            PZCommon.CategoryMenuButton {
                id: zoneNamePicker

                items: zoneNamesRow._addable
                enabled: zoneNamesRow._addable.length > 0
                currentId: ""
                includeNoneEntry: false
                placeholderText: i18nc("@action:button", "Add a name…")
                Accessible.description: i18nc("@info:accessibility", "Add a zone name from your layouts")
                onSelected: function (id) {
                    if (!id || id.length === 0)
                        return;
                    // The typed text may hold an uncommitted edit: merge it
                    // rather than discard it, then append the picked name.
                    var next = row.controller ? Array.from(row.controller.parseZoneNameList(zoneNamesField.text)) : zoneNamesRow._names.slice();
                    for (var i = 0; i < next.length; i++) {
                        if (String(next[i]).trim().toLowerCase() === String(id).trim().toLowerCase())
                            return;
                    }
                    next.push(id);
                    zoneNamesRow.commit(next);
                }
            }
        }
    }

    // Comma/space/range-separated zone-number input for `kind == "zoneOrdinals"`
    // (SnapToZone). Stores a JSON array of 1-based ordinals; multiple ordinals
    // span their combined area. Accepts "1, 2", "1;2", "1 2", and ranges "1-3".
    property Component _zoneOrdinalsEditor: Component {
        TextField {
            readonly property var _param: parent.modelData
            readonly property var _zones: Array.isArray(row.action[_param.key]) ? row.action[_param.key] : []
            // The SnapToZone ordinal cap (PhosphorRules::MaxZoneOrdinal,
            // published by the descriptor as this param's `max`). Both parse
            // paths below clamp against it: the range path so an unbounded
            // "1-100000" cannot build a huge array on the UI thread, and the
            // single-ordinal path so a typed "70" is dropped at parse time
            // rather than committed as a value the validator rejects.
            readonly property real _maxZoneOrdinal: editors._snapToZoneBound(_param.key)

            // Normalised display (sorted, deduped) re-binds after each edit.
            text: _zones.join(", ")
            placeholderText: i18nc("@info:placeholder zone numbers for a snap-to-zone rule", "e.g. 1, 2 or 1-2")
            Accessible.name: _param.label
            Accessible.description: i18nc("@info:accessibility zone numbers field of a snap-to-zone rule", "One or more 1-based zone numbers to snap matched windows to. Multiple zones span their combined area.")
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
                        // A range expands to one entry per ordinal, so it needs
                        // a finite cap to be safe on the UI thread; a schema
                        // that publishes none cannot bound it, and the range is
                        // refused rather than expanded unbounded.
                        if (!isFinite(_maxZoneOrdinal))
                            continue;
                        var lo = parseInt(range[1], 10);
                        var hi = parseInt(range[2], 10);
                        if (hi > _maxZoneOrdinal)
                            hi = _maxZoneOrdinal;
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
                        if (n >= 1 && n <= _maxZoneOrdinal && !seen[n]) {
                            seen[n] = true;
                            parsed.push(n);
                        }
                    }
                }
                parsed.sort(function (a, b) {
                    return a - b;
                });
                // A SnapToZone action needs at least one target across its
                // ordinal and name lists (the descriptor validator rejects an
                // action with neither). If the user cleared the field or typed
                // only invalid tokens AND no zone names are set, keep the last
                // valid value rather than committing an empty list — that would
                // leave an action the validator rejects (Save is gated on it,
                // with a generic footer message that does not name the field).
                // Restore via Qt.binding (not a bare `text = ...`) so the
                // declarative `text: _zones.join(", ")` binding survives and
                // keeps normalising on later edits. With names present, an
                // empty ordinal list is a legitimate "names only" target.
                if (parsed.length === 0 && !editors._snapToZoneHasNames(row.action)) {
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
    // colour on SetBorderColorActive / SetBorderColorInactive / SetTintColor
    // plus the overlay-colour actions. Stores a `#AARRGGBB` wire string
    // (alpha-first, matching QColor::HexArgb and the global zone/border
    // colours) so transparency set in the picker survives. The validator
    // accepts the `#AARRGGBB` shape and the effect-side consumer parses it via
    // `QColor(QString)` (which reads 9-digit hex alpha-first).
    //
    // Presented through ThemeFallbackColorControl — the same swatch, hex
    // label, and reset every settings-page colour row uses — with "accent" as
    // the sentinel. Only the three border/tint actions carry the accent
    // concept (their consumer resolves the token; the overlay-colour actions'
    // validator is plain hex), so Reset exists only on those.
    property Component _colorParamEditor: Component {
        RowLayout {
            id: colorEditorRoot

            readonly property var _param: parent.modelData
            // Final fallback (colour param with no stored value) derives from
            // the theme's accent rather than a hardcoded hex (CLAUDE.md:
            // never hardcode colors). Unreachable in practice —
            // defaultPayloadFor always seeds a colour param.
            readonly property string _hex: (row.action[_param.key] !== undefined && row.action[_param.key] !== "") ? String(row.action[_param.key]) : String(Kirigami.Theme.highlightColor)
            // Whether this action's consumer resolves the "accent" sentinel:
            // published on the param descriptor (acceptsAccent) so this
            // editor, defaultPayloadFor's seeding, and ActionRow's
            // type-switch migration all read one C++ source of truth.
            readonly property bool _accentCapable: colorEditorRoot._param.acceptsAccent === true
            // The accent sentinel follows the system colour scheme per focus state,
            // the same split updateWindowDecoration applies: the focused (active) slot
            // adopts the highlight colour, the unfocused (inactive) slot the inactive
            // colour. Preview the matching system colour WITH its alpha so the swatch
            // shows what the border will actually draw instead of one opaque accent
            // for both. Falls back to the theme highlight when settings are absent.
            readonly property color _accentColor: !row.appSettings ? Kirigami.Theme.highlightColor : (row.action.type === "setBorderColorInactive" ? row.appSettings.inactiveColor : row.appSettings.highlightColor)

            spacing: Kirigami.Units.smallSpacing

            ThemeFallbackColorControl {
                storedColor: colorEditorRoot._hex
                sentinel: "accent"
                fallbackLabel: i18n("Accent")
                themeColor: colorEditorRoot._accentColor
                // Reset only where the sentinel means something: writing
                // "accent" into an overlay-colour action would fail its
                // plain-hex validator.
                showReset: colorEditorRoot._accentCapable
                resetAccessibleName: i18n("Reset to the system accent color")
                resetToolTip: i18n("Follow the system accent color")
                // The PAGE-LEVEL colour picker (via the appSettings bridge)
                // rather than a delegate-scoped ColorDialog: a rule-list
                // rebuild while the dialog is open would destroy this delegate
                // and tear the popup down under the user.
                picker: row.appSettings ? row.appSettings.colorPicker : null
                swatchAccessibleName: colorEditorRoot._param.label
                onColorChosen: function (hex) {
                    row.actionEdited(row._withParam(colorEditorRoot._param.key, hex));
                }
            }

            Item {
                Layout.fillWidth: true
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

    // The snapping-layout and tiling-algorithm action editors use the rich
    // `LayoutComboBox` — preview tile + category / aspect badges, same
    // component the assignment pages use. `layoutFilter` separates the two
    // streams: 0 = manual / snapping layouts, 1 = autotile algorithms.
    // `showNoneOption: false` because the action either targets a layout or
    // has no value (no implicit "Default" fallback inside a rule). The
    // explicit-none row is a different thing and both editors carry it: the
    // reserved word is a real value the action can carry (opt the context
    // out of layouts entirely), so without the row a stored token had no
    // entry to match and could not be re-picked. The scrolling-template
    // editor below uses a plain combo instead, for the reason its own
    // comment gives.
    property Component _snappingLayoutEditor: Component {
        LayoutComboBox {
            readonly property var _param: parent.modelData

            Accessible.name: _param.label
            appSettings: row.appSettings
            currentLayoutId: row.action[_param.key] || ""
            layoutFilter: 0
            showNoneOption: false
            // The reserved "explicitly no layout" word
            // (PhosphorZones::NoSnappingLayout, whose spelling checklist
            // names this site).
            showExplicitNoneOption: true
            explicitNoneValue: editors.noLayoutToken
            showPreview: true
            onActivated: function (index) {
                row.actionEdited(row._withParam(_param.key, currentValue));
            }
        }
    }

    // Native scrolling-template picker for SetScrollingTemplate. A plain
    // combo over the template family in the shared layouts model — the rich
    // LayoutComboBox streams are keyed to manual/autotile categories, and a
    // template row needs neither aspect badges nor category chips. A stored
    // id whose template was deleted still surfaces verbatim.
    property Component _scrollingTemplateEditor: Component {
        WideComboBox {
            id: templateCombo

            readonly property var _param: parent.modelData
            // The reserved "explicitly no template" word the action can carry,
            // as opposed to an empty slot, which inherits the configured
            // default. The authoritative declaration is
            // PhosphorZones::NoScrollingTemplate in AssignmentEntry.h, whose
            // spelling checklist names this site.
            readonly property string _noTemplateToken: "none"
            readonly property var _templates: {
                let entries = [];
                const all = (row.appSettings && row.appSettings.layouts) || [];
                for (let i = 0; i < all.length; i++) {
                    if (all[i].isScrollingTemplate === true)
                        entries.push({
                            "label": all[i].displayName,
                            "id": all[i].id
                        });
                }
                // The reserved "explicitly no template" word, last, the way
                // every other template list in the app orders it (the unified
                // layout list sorts it there and the layout picker reads it off
                // the tail). It is a real value the action can carry, so
                // without a row for it the stored token had no entry to match
                // and fell through to the raw-id displayText below, printing
                // "none" at the user — and an action already set to it could
                // not be re-picked once changed.
                entries.push({
                    "label": i18nc("@item:inlistbox scrolling template rule action, use no template at all", "None"),
                    "id": templateCombo._noTemplateToken
                });
                return entries;
            }

            model: _templates
            textRole: "label"
            valueRole: "id"
            currentIndex: {
                var target = row.action[_param.key] || "";
                for (var i = 0; i < templateCombo._templates.length; ++i) {
                    if (templateCombo._templates[i].id === target)
                        return i;
                }
                return -1;
            }
            displayText: currentIndex >= 0 ? currentText : (row.action[_param.key] || i18n("Choose a template…"))
            Accessible.name: _param.label
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
            readonly property var _screens: (row.appSettings && row.appSettings.screens) || []
            model: _screens.map(function (s) {
                var label = s.displayLabel || s.name || "";
                if (s.isPrimary)
                    // Composed inside one i18nc so translators control the
                    // order and the separator survives RTL bidi runs.
                    label = i18nc("monitor name, then the primary-monitor marker", "%1 · %2", label, i18n("Primary"));
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
            readonly property var _names: (row.appSettings && row.appSettings.virtualDesktopNames) || []
            model: {
                var items = [];
                for (var i = 1; i <= desktopCombo._count; ++i) {
                    var name = desktopCombo._names.length >= i ? desktopCombo._names[i - 1] : "";
                    items.push({
                        // Composed inside one i18nc so translators control the
                        // order and the separator, matching the monitor picker
                        // above and the match-side desktop picker.
                        "label": name && name.length > 0 ? i18nc("virtual desktop number, then its name", "%1: %2", i, name) : ("" + i),
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
                // The reserved word stays bare: the explicit-none row is keyed
                // "none", and prefixing it would leave the combo blank for a
                // stored opt-out. A hand-edited "autotile:none" gets the same
                // tolerance the corrupted prefixed real ids get — mapped to
                // the bare word so it seats the None row instead of leaving
                // the combo blank.
                const stored = row.action[_param.key] || "";
                if (stored === "autotile:" + editors.noLayoutToken)
                    return editors.noLayoutToken;
                if (stored === "" || stored === editors.noLayoutToken || stored.startsWith("autotile:"))
                    return stored;
                return "autotile:" + stored;
            }
            layoutFilter: 1
            showNoneOption: false
            // The reserved "explicitly no algorithm" word
            // (PhosphorZones::NoTilingAlgorithm, whose spelling checklist
            // names this site). Bare on the wire like the algorithm ids.
            // NOT offered for setAlgorithmParam, which shares this editor by
            // kind: parameters tuned for "no algorithm" is a meaningless
            // payload, and the SetAlgorithmParam summary would render the raw
            // word as an algorithm apparently named "none".
            showExplicitNoneOption: row.action.type !== "setAlgorithmParam"
            explicitNoneValue: editors.noLayoutToken
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
                onClicked: {
                    // Open the PAGE-LEVEL curve editor (via the appSettings
                    // bridge) rather than a delegate-scoped one, so a rule-list
                    // rebuild while it is open cannot tear the popup down under
                    // the user. The target param key is captured in the
                    // transient handlers, which self-disconnect on close.
                    var picker = row.appSettings ? row.appSettings.curvePicker : null;
                    if (!picker)
                        return;
                    var key = curveSlot._param.key;
                    function cleanup() {
                        picker.curveApplied.disconnect(curveHandler);
                        picker.springApplied.disconnect(springHandler);
                        picker.closed.disconnect(cleanup);
                    }
                    function curveHandler(curve) {
                        cleanup();
                        row.actionEdited(row._withParam(key, curve));
                    }
                    function springHandler(omega, zeta) {
                        cleanup();
                        row.actionEdited(row._withParam(key, "spring:" + omega.toFixed(2) + "," + zeta.toFixed(2)));
                    }
                    picker.curveApplied.connect(curveHandler);
                    picker.springApplied.connect(springHandler);
                    picker.closed.connect(cleanup);
                    // Through the controller's humaniser rather than the raw
                    // dotted path (eventLabel() returns the English form by
                    // contract, so this one word is untranslated — a rule's
                    // free-form event field has no i18n literal to pass).
                    picker.openFor({
                        "eventLabel": (row.action.event && row.appSettings && row.appSettings.animationsController) ? row.appSettings.animationsController.eventLabel(row.action.event) : "",
                        "timingMode": curveSlot._isSpring ? CurvePresets.timingModeSpring : CurvePresets.timingModeEasing,
                        "easingCurve": curveSlot._easingCurve,
                        "springOmega": curveSlot._springOmega,
                        "springZeta": curveSlot._springZeta,
                        "duration": row.appSettings ? row.appSettings.animationDuration : CurvePresets.defaultDurationMs
                    });
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
        // Cascading category menu (the same widget ActionRow.qml uses for its
        // action-type picker, and the animations page for its shader picker)
        // instead of a flat combo, so
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
