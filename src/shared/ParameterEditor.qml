// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable parameter editor for a schema-described parameter set.
 *
 * Renders a labelled control per parameter (slider / spinbox / switch /
 * combo / colour / image) from a descriptor list, with an optional
 * Lock-all / Randomize toolbar and flat-or-grouped rows. Shader effects,
 * decoration packs, and autotile algorithm params all feed it; each host
 * toggles the affordances it wants via the `enable*` flags below.
 *
 * The descriptor shape is `{ id, name, type, default?, min?, max?, step?,
 * description?, group?, enumOptions? }`. `type` is one of `number`/`float`
 * (interchangeable), `int`, `bool`, `enum`, `color`, `image`.
 *
 * The component is fully props-and-signals — it does not own state.
 * The host:
 *   - feeds `parameters` (the schema) and `currentValues` (the live map)
 *   - listens for `valueChanged(id, value)` and writes back into its map
 *   - optionally listens for `lockToggled` / `randomizeRequested` /
 *     `lockAllRequested` and updates its locks map / runs randomize
 *   - listens for `requestColorPicker` / `requestImagePicker` and opens
 *     the platform dialog at the parent level (image dialogs in
 *     particular need to outlive their delegate row)
 *
 * Three pure helpers (`lockedAfterToggle`, `lockedAfterAllToggle`,
 * `computeRandomized`) compute new maps from the component's current
 * props for hosts that wire the lock + randomize toolbar — both
 * consumers route their handler bodies through them so the transforms
 * live in one place.
 *
 * Accordion behavior: when `enableGroups: true` and the parameter list
 * declares any `group` field, rows are split into ParameterSection
 * accordions. Exactly one group is expanded at a time;
 * `expandedGroupIndex` is read/write so the host can choose "first
 * open" (default 0), open a specific group (any non-negative index), or
 * collapse everything (-1).
 */
ColumnLayout {
    id: root

    required property var parameters
    required property var currentValues
    property var lockedParams: ({})
    property bool enableLocking: true
    property bool enableRandomize: true
    /// Show the header "reset all to defaults" button (left of Lock-All).
    /// Defaults to `enableRandomize` so it appears wherever the bulk
    /// randomize does; a host with its own reset affordance (the shader
    /// browser's "Default" button) sets this false to avoid duplication.
    property bool enableReset: enableRandomize
    property bool enableGroups: true
    property bool enableImage: true
    /// Compact mode renders rows in the settings-app style: title left,
    /// fixed-width control on the right, indented to match `SettingsRow`.
    /// Default (false) keeps the editor's wide-slider aesthetic.
    property bool compact: false
    property int expandedGroupIndex: 0
    // Visual size hints forwarded to ParameterRow. Defaults shift in
    // compact mode to match the settings-app SettingsSlider sizing.
    property int sliderValueLabelWidth: compact ? Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 4
    property int colorButtonSize: compact ? Kirigami.Units.gridUnit * 2 : Kirigami.Units.gridUnit * 3
    property int colorLabelWidth: compact ? Kirigami.Units.gridUnit * 5 : Kirigami.Units.gridUnit * 7
    property int labelColumnWidth: Kirigami.Units.gridUnit * 8
    /// The toolbar row (Lock-all / Randomize) is on by default but can
    /// be hidden when neither button is wanted (animation settings).
    readonly property bool _showToolbar: enableLocking || enableRandomize || enableReset || toolbarTrailing !== null
    /// Show the "Parameters" heading independently of the lock / randomize
    /// buttons. Defaults to the toolbar's own visibility so existing
    /// consumers are unchanged; a host with both buttons disabled (e.g. the
    /// rule-action algorithm-param editor) sets this true to keep the heading.
    property bool showParametersHeader: _showToolbar
    /// Optional trailing item for the toolbar row, instantiated after the
    /// Lock-all / Randomize buttons. The editor dialog uses it for its
    /// metadata-driven preset menu so the affordance stays in line with
    /// the other parameter actions; setting it to `null` (default) keeps
    /// the toolbar pristine for hosts that don't need extras.
    property Component toolbarTrailing: null
    readonly property var _parameterGroups: {
        if (!enableGroups || !parameters || parameters.length === 0)
            return [];

        var hasGroups = false;
        for (var i = 0; i < parameters.length; i++) {
            if (parameters[i] && parameters[i].group) {
                hasGroups = true;
                break;
            }
        }
        if (!hasGroups)
            return [];

        var defaultGroupName = i18nc("@title:group", "General");
        var groupMap = {};
        var groupOrder = [];
        for (var j = 0; j < parameters.length; j++) {
            var param = parameters[j];
            if (!param)
                continue;

            var groupName = param.group || defaultGroupName;
            if (!groupMap[groupName]) {
                groupMap[groupName] = [];
                groupOrder.push(groupName);
            }
            groupMap[groupName].push(param);
        }
        var result = [];
        for (var k = 0; k < groupOrder.length; k++) {
            var name = groupOrder[k];
            result.push({
                "name": name,
                "params": groupMap[name]
            });
        }
        return result;
    }
    readonly property bool _hasAnyLocked: {
        if (!lockedParams)
            return false;

        var keys = Object.keys(lockedParams);
        for (var i = 0; i < keys.length; i++) {
            if (lockedParams[keys[i]] === true)
                return true;
        }
        return false;
    }
    // ── Row delegates ────────────────────────────────────────────────
    /// Editor (wide) row: fixed-width title column on the left, fillWidth
    /// control on the right. Mirrors the inline layout the dialog used
    /// before extraction.
    property Component _wideRowComponent
    /// Settings (compact) row: fillWidth title on the left, fixed-width
    /// control on the right. Layout mirrors the settings-app SettingsRow
    /// so per-event shader params line up with the timing-mode and
    /// duration rows above them. The control's 45%-max-width clamp is
    /// enforced by the enclosing SettingsRow, NOT here — this delegate
    /// only sets the Layout.preferred sizes; SettingsRow caps them.
    property Component _compactRowComponent

    signal valueChanged(string paramId, var value)
    /// Emitted once per affected param. A group-lock click fans out into
    /// N synchronous emissions (one per param in the group). Hosts that
    /// store locks in a single map and reassign atomically (the standard
    /// pattern) coalesce the N emissions into one render.
    signal lockToggled(string paramId, bool locked)
    signal lockAllRequested(bool locked)
    signal randomizeRequested
    /// Reset every parameter to its schema default. Arg-less like
    /// `randomizeRequested`: the host computes the defaults map via
    /// `computeDefaults()` (or its own equivalent) and persists it in one
    /// batch write, mirroring the randomize round-trip.
    signal resetRequested
    signal requestColorPicker(string paramId, string paramName, color current)
    signal requestImagePicker(string paramId)

    // ── Pure helpers for the host's lock / randomize plumbing ───────
    /// Toggle the lock for one parameter. Returns a new lockedParams
    /// map; the host assigns it back into its own state property.
    /// Lives here so the editor and settings card don't carry their
    /// own copy of the same lookup-and-rewrite boilerplate.
    function lockedAfterToggle(paramId, locked) {
        var next = Object.assign({}, root.lockedParams || {});
        if (locked)
            next[paramId] = true;
        else
            delete next[paramId];
        return next;
    }

    /// Lock-or-unlock-every-parameter map. Reads `parameters` from
    /// the component instance.
    function lockedAfterAllToggle(locked) {
        var next = {};
        if (locked && root.parameters) {
            for (var i = 0; i < root.parameters.length; i++) {
                var p = root.parameters[i];
                if (p && p.id !== undefined)
                    next[p.id] = true;
            }
        }
        return next;
    }

    /// Coerce @p v to a finite number, falling back to @p fallback for
    /// undefined / non-numeric / NaN / Infinity. Mirrors ParameterRow's
    /// `_numberOr` boundary defence: raw JSON metadata bounds can be strings
    /// or NaN and must not leak into computed values.
    function _finiteOr(v, fallback) {
        var n = Number(v);
        return isFinite(n) ? n : fallback;
    }

    /// Roll a fresh value for every unlocked parameter within its
    /// metadata range. Locked entries and `image`-typed entries are
    /// preserved verbatim from `currentValues` (or the param default
    /// when missing). Returns a new full values map. Hosts can
    /// either feed it back via `currentValues` (editor pattern) or
    /// push it through their write-back API (settings pattern).
    function computeRandomized() {
        var next = {};
        if (!root.parameters)
            return next;

        for (var i = 0; i < root.parameters.length; i++) {
            var param = root.parameters[i];
            if (!param || param.id === undefined)
                continue;

            if ((root.lockedParams && root.lockedParams[param.id] === true) || param.type === "image") {
                // Mirror the switch-path's undefined guard below — a
                // schema with a missing `default` and a currentValues
                // map missing this id would otherwise write
                // `undefined` into the result, and a downstream
                // `setShaderOverride` would persist a malformed entry.
                var preserved = (root.currentValues && root.currentValues[param.id] !== undefined) ? root.currentValues[param.id] : param.default;
                if (preserved !== undefined)
                    next[param.id] = preserved;

                continue;
            }
            var value = root._rollParam(param);
            if (value !== undefined)
                next[param.id] = value;
        }
        return next;
    }

    /// Roll a fresh value for one @p param within its metadata range,
    /// returning it (or `undefined` for an unrollable type). Shared by
    /// the bulk `computeRandomized` and the per-row `_randomizeOne` so the
    /// per-type roll logic lives in one place.
    function _rollParam(param) {
        switch (param.type) {
        case "number":
        case "float":
            // Coerce bounds/step to finite numbers: raw JSON metadata can
            // carry strings/NaN, which would otherwise yield string
            // concatenation ("0.5" + ...) or NaN in the persisted value.
            var minF = root._finiteOr(param.min, 0);
            var maxF = root._finiteOr(param.max, 1);
            var stepF = root._finiteOr(param.step, 0);
            var f = minF + Math.random() * (maxF - minF);
            if (stepF > 0) {
                f = Math.round(f / stepF) * stepF;
                // Step-rounding can escape the range when a bound is not
                // a step multiple (e.g. max 1.6, step 1 -> 2.0); the
                // emitted value is what persists, so clamp it back.
                f = Math.min(maxF, Math.max(minF, f));
            }
            return f;
        case "int":
            var minI = Math.round(root._finiteOr(param.min, 0));
            var maxI = Math.round(root._finiteOr(param.max, 100));
            // Clamp defensively — fractional/garbage bounds can push the
            // draw above max (same range-escape class as the float step).
            return Math.min(maxI, Math.max(minI, Math.floor(minI + Math.random() * (maxI - minI + 1))));
        case "bool":
            return Math.random() < 0.5;
        case "enum":
            var opts = param.enumOptions || [];
            // Preserve the current value when the vocabulary is empty
            // rather than writing undefined into the rolled map.
            return opts.length > 0 ? opts[Math.floor(Math.random() * opts.length)] : param.default;
        case "color":
            var r = Math.floor(Math.random() * 256);
            var g = Math.floor(Math.random() * 256);
            var b = Math.floor(Math.random() * 256);
            return "#" + r.toString(16).padStart(2, "0") + g.toString(16).padStart(2, "0") + b.toString(16).padStart(2, "0");
        default:
            return param.default;
        }
    }

    /// Roll a single parameter and emit it through `valueChanged` (the
    /// normal per-param write path — a per-row randomize is one value, so
    /// unlike the bulk reset/randomize it needs no batch signal). `image`
    /// params are skipped (no sensible random image); the per-row button
    /// is hidden for them anyway.
    function _randomizeOne(paramId) {
        if (!root.parameters)
            return;

        for (var i = 0; i < root.parameters.length; i++) {
            var p = root.parameters[i];
            if (!p || p.id !== paramId)
                continue;

            if (p.type === "image")
                return;

            var value = root._rollParam(p);
            if (value !== undefined)
                root.valueChanged(paramId, value);
            return;
        }
    }

    /// Every parameter's schema default, keyed by id. Params with no
    /// declared default are omitted (a reset can't invent one). Hosts feed
    /// it back through their batch-persist path in the `resetRequested`
    /// handler, mirroring how `computeRandomized` feeds the randomize path.
    function computeDefaults() {
        var next = {};
        if (!root.parameters)
            return next;

        for (var i = 0; i < root.parameters.length; i++) {
            var param = root.parameters[i];
            if (param && param.id !== undefined && param.default !== undefined)
                next[param.id] = param.default;
        }
        return next;
    }

    spacing: Kirigami.Units.smallSpacing

    // ── Toolbar (Lock-all / Randomize) ───────────────────────────────
    RowLayout {
        Layout.fillWidth: true
        visible: (root.showParametersHeader || root._showToolbar) && root.parameters && root.parameters.length > 0
        spacing: Kirigami.Units.smallSpacing

        Label {
            Layout.fillWidth: true
            text: i18nc("@title:group", "Parameters")
            font.weight: Font.DemiBold
            visible: root.showParametersHeader
        }

        ToolButton {
            visible: root.enableReset
            icon.name: "edit-reset"
            display: ToolButton.IconOnly
            ToolTip.text: i18nc("@info:tooltip", "Reset all parameters to their defaults")
            Accessible.name: i18nc("@action:button", "Reset to Defaults")
            Accessible.description: ToolTip.text
            onClicked: root.resetRequested()
        }

        ToolButton {
            visible: root.enableLocking
            icon.name: root._hasAnyLocked ? "object-unlocked" : "object-locked"
            display: ToolButton.IconOnly
            ToolTip.text: root._hasAnyLocked ? i18nc("@info:tooltip", "Unlock all parameters") : i18nc("@info:tooltip", "Lock all parameters")
            Accessible.name: root._hasAnyLocked ? i18nc("@action:button", "Unlock All") : i18nc("@action:button", "Lock All")
            Accessible.description: ToolTip.text
            onClicked: root.lockAllRequested(!root._hasAnyLocked)
        }

        ToolButton {
            visible: root.enableRandomize
            icon.name: "roll"
            display: ToolButton.IconOnly
            ToolTip.text: root._hasAnyLocked ? i18nc("@info:tooltip", "Randomize unlocked parameters") : i18nc("@info:tooltip", "Randomize all parameters")
            Accessible.name: i18nc("@action:button", "Random")
            Accessible.description: ToolTip.text
            onClicked: root.randomizeRequested()
        }

        Loader {
            active: root.toolbarTrailing !== null
            sourceComponent: root.toolbarTrailing
        }
    }

    // ── Empty state when no parameters ───────────────────────────────
    Label {
        Layout.fillWidth: true
        // Show the empty-state for both `parameters: undefined` and
        // `parameters: []` — undefined would otherwise hide every layout
        // branch and produce a blank component.
        visible: !root.parameters || root.parameters.length === 0
        text: i18nc("@info", "This effect has no configurable parameters.")
        wrapMode: Text.WordWrap
        opacity: 0.7
    }

    // ── Grouped (accordion) layout ───────────────────────────────────
    ColumnLayout {
        Layout.fillWidth: true
        visible: root._parameterGroups.length > 0
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            id: groupRepeater

            model: root._parameterGroups

            delegate: ParameterSection {
                id: paramSection

                required property var modelData
                required property int index

                Layout.fillWidth: true
                title: modelData.name
                groupParams: modelData.params
                expanded: root.expandedGroupIndex === index
                lockedParams: root.lockedParams
                enableLocking: root.enableLocking
                onToggled: {
                    root.expandedGroupIndex = expanded ? -1 : index;
                }
                onGroupLockToggled: function (lock) {
                    // Synthesise per-id `lockToggled` signals so the host
                    // reacts with the same handler it uses for single-row
                    // toggles. The host typically batches-replaces its
                    // lockedParams map, so the N notifications cost one
                    // map mutation per click in practice — fan-out here
                    // keeps the public signal surface uniform.
                    for (var j = 0; j < paramSection.groupParams.length; j++) {
                        var pp = paramSection.groupParams[j];
                        if (pp && pp.id !== undefined)
                            root.lockToggled(pp.id, lock);
                    }
                }

                contentComponent: Component {
                    ColumnLayout {
                        Kirigami.Theme.inherit: true
                        spacing: Kirigami.Units.smallSpacing

                        Repeater {
                            model: paramSection.groupParams
                            delegate: root.compact ? root._compactRowComponent : root._wideRowComponent
                        }
                    }
                }
            }
        }
    }

    // ── Flat layout (no groups declared, or enableGroups disabled) ───
    ColumnLayout {
        Layout.fillWidth: true
        visible: root._parameterGroups.length === 0 && root.parameters && root.parameters.length > 0
        spacing: Kirigami.Units.smallSpacing

        Repeater {
            model: root._parameterGroups.length === 0 ? root.parameters : []
            delegate: root.compact ? root._compactRowComponent : root._wideRowComponent
        }
    }

    _wideRowComponent: Component {
        RowLayout {
            required property var modelData
            required property int index

            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            Label {
                Layout.preferredWidth: root.labelColumnWidth
                Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                text: modelData ? (modelData.name || modelData.id || "") : ""
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
            }

            ParameterRow {
                Layout.fillWidth: true
                Kirigami.Theme.inherit: true
                compact: false
                paramData: modelData
                currentValues: root.currentValues
                lockedParams: root.lockedParams
                enableLocking: root.enableLocking
                enableRandomize: root.enableRandomize
                enableImage: root.enableImage
                sliderValueLabelWidth: root.sliderValueLabelWidth
                colorButtonSize: root.colorButtonSize
                colorLabelWidth: root.colorLabelWidth
                onValueChanged: function (id, value) {
                    root.valueChanged(id, value);
                }
                onLockToggled: function (id, locked) {
                    root.lockToggled(id, locked);
                }
                onRandomizeRequested: function (id) {
                    root._randomizeOne(id);
                }
                onRequestColorPicker: function (id, name, current) {
                    root.requestColorPicker(id, name, current);
                }
                onRequestImagePicker: function (id) {
                    root.requestImagePicker(id);
                }
            }
        }
    }

    _compactRowComponent: Component {
        RowLayout {
            required property var modelData
            required property int index

            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: Kirigami.Units.gridUnit * 10
                spacing: Math.round(Kirigami.Units.smallSpacing / 2)

                Label {
                    text: modelData ? (modelData.name || modelData.id || "") : ""
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }

                Label {
                    // `&&` short-circuits to its first falsy operand — when
                    // modelData is undefined the whole chain returns undefined,
                    // which QML can't coerce to bool/string. Wrap visibility in
                    // `!!` and the text in an explicit ternary so the property
                    // assignments always receive concrete typed values.
                    readonly property string _description: (modelData && typeof modelData.description === "string") ? modelData.description : ""

                    text: _description
                    Layout.fillWidth: true
                    visible: _description.length > 0
                    font: Kirigami.Theme.smallFont
                    color: Kirigami.Theme.disabledTextColor
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                }
            }

            ParameterRow {
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                Kirigami.Theme.inherit: true
                compact: true
                paramData: modelData
                currentValues: root.currentValues
                lockedParams: root.lockedParams
                enableLocking: root.enableLocking
                enableRandomize: root.enableRandomize
                enableImage: root.enableImage
                sliderValueLabelWidth: root.sliderValueLabelWidth
                colorButtonSize: root.colorButtonSize
                colorLabelWidth: root.colorLabelWidth
                onValueChanged: function (id, value) {
                    root.valueChanged(id, value);
                }
                onLockToggled: function (id, locked) {
                    root.lockToggled(id, locked);
                }
                onRandomizeRequested: function (id) {
                    root._randomizeOne(id);
                }
                onRequestColorPicker: function (id, name, current) {
                    root.requestColorPicker(id, name, current);
                }
                onRequestImagePicker: function (id) {
                    root.requestImagePicker(id);
                }
            }
        }
    }
}
