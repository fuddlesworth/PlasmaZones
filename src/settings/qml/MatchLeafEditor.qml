// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as PZCommon

/**
 * @brief One leaf predicate row — `{ field, op, value }`.
 *
 * The field is a categorized picker and the operator is a dropdown; the value
 * field shape adapts to the field's value kind (string / number / bool).
 * Two-way: edits emit `leafChanged(updatedLeaf)`.
 */
RowLayout {
    id: leaf

    /// The leaf JSON object — `{ field: "appId", op: "equals", value: ... }`.
    required property var node
    /// The RuleController (for operatorsForField).
    required property var controller
    /// The SettingsController — populates the screen / activity pickers.
    required property var appSettings
    // Each entry carries `{ value, wire, label, valueKind }` — the controller
    // owns the enum↔wire-string table; this component never reconstructs it.
    // Cached once at RuleEditorSheet (matchFields() is a Q_INVOKABLE that
    // allocates a fresh list per call) and threaded down via MatchExpressionEditor.
    required property var fieldOptions
    // The current field's descriptor (by wire string), or undefined if the
    // stored field is unknown / legacy.
    readonly property var _fieldEntry: leaf._entryForWire(leaf.fieldOptions, leaf.node.field)
    // True once a field is chosen. A freshly-added condition seeds an empty
    // field so the picker shows its placeholder and the user must select one;
    // the operator / value editors stay hidden until then (mirroring how an
    // action row shows no param editors until its type is picked). Keyed on
    // the field STRING being non-empty — not on `_fieldEntry` — so a legacy /
    // unknown field (non-empty but unrecognised) still shows its editors.
    readonly property bool _hasField: leaf.node.field !== undefined && String(leaf.node.field).length > 0
    // Operator options depend on the current field's enum value.
    readonly property var _operatorOptions: leaf._fieldEntry !== undefined ? controller.operatorsForField(leaf._fieldEntry.value) : []
    readonly property string _valueKind: leaf._fieldEntry !== undefined ? leaf._fieldEntry.valueKind : "string"
    // Widest operator label across the FULL operator vocabulary, in pixels —
    // the operator dropdown is sized to this (not to the current field's
    // operator subset) so the operator column lines up on every condition row
    // and doesn't resize when the field changes. Recomputed by
    // _recalcOperatorWidth() on load and whenever the measuring font changes.
    property real _widestOperatorTextWidth: 0

    signal leafChanged(var updatedLeaf)
    signal removeRequested

    /// The descriptor in @p options whose `wire` equals @p wire, or undefined.
    function _entryForWire(options, wire) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].wire === wire)
                return options[i];
        }
        return undefined;
    }

    /// The index of @p wire in @p options, or -1 when unknown — an unknown
    /// (legacy) value must surface as no selection, never coerce to index 0.
    function _indexForWire(options, wire) {
        for (var i = 0; i < options.length; ++i) {
            if (options[i].wire === wire)
                return i;
        }
        return -1;
    }

    function _emit(field, op, value) {
        leaf.leafChanged({
            "field": field,
            "op": op,
            "value": value
        });
    }

    /// Measure every operator label (the full vocabulary, via
    /// controller.allOperators()) and cache the widest in pixels. The label
    /// set is static, so this only needs to run on load and on font change.
    function _recalcOperatorWidth() {
        var ops = leaf.controller ? leaf.controller.allOperators() : [];
        var maxW = 0;
        for (var i = 0; i < ops.length; ++i) {
            opMetrics.text = ops[i].label || "";
            maxW = Math.max(maxW, opMetrics.advanceWidth);
        }
        leaf._widestOperatorTextWidth = maxW;
    }

    /// True when the running-windows picker has a mode that fills @p wire
    /// — AppId, WindowClass, DesktopFile and Title all have a 1:1 mode in
    /// the WindowPickerDialog. Other string fields (e.g. WindowRole on a
    /// rare X11 client) stay freeform.
    function _fieldIsPickable(wire) {
        return wire === "appId" || wire === "windowClass" || wire === "desktopFile" || wire === "title";
    }

    /// Open the running-windows picker on the leaf's behalf in the mode
    /// that maps to the current field. The picker lives at the page level
    /// (hosting it inside the OverlaySheet collapsed its content area),
    /// shared across every leaf in every open rule editor — so we
    /// dynamically wire a one-shot `picked` handler that fills THIS leaf
    /// and then disconnects itself, instead of permanently binding the
    /// dialog's `onPicked`. A matching `closed` listener disconnects the
    /// handler when the user dismisses the dialog without picking
    /// (Escape / outside-click) — without it the stale handler would
    /// still be wired the next time ANY leaf opened the picker, and one
    /// pick would fan out to every previously-cancelled leaf.
    function _openWindowPicker() {
        function pickedHandler(value) {
            picker.picked.disconnect(pickedHandler);
            picker.closed.disconnect(closedHandler);
            leaf._emit(leaf.node.field, leaf.node.op, value);
        }

        function closedHandler() {
            // `picked` already fired and self-disconnected before `closed`
            // runs (Kirigami emits `closed` AFTER the close completes);
            // disconnecting again is a no-op when the handler is no
            // longer wired, so this branch handles dismissal without
            // double-firing on a successful pick.
            picker.picked.disconnect(pickedHandler);
            picker.closed.disconnect(closedHandler);
        }

        var picker = leaf.appSettings ? leaf.appSettings.windowPicker : null;
        if (!picker)
            return;

        picker.picked.connect(pickedHandler);
        picker.closed.connect(closedHandler);
        if (leaf.node.field === "appId")
            picker.openForApps();
        else if (leaf.node.field === "desktopFile")
            picker.openForDesktopFiles();
        else if (leaf.node.field === "title")
            picker.openForTitles();
        else
            picker.openForClasses();
    }

    /// Short tooltip / accessible-name suffix for the pick button. Saves
    /// the inline ternary chain from repeating in two places.
    function _pickTooltip(wire) {
        if (wire === "appId")
            return i18n("Pick from running applications");

        if (wire === "desktopFile")
            return i18n("Pick desktop file from running windows");

        if (wire === "title")
            return i18n("Pick title from running windows");

        return i18n("Pick from running windows");
    }

    spacing: Kirigami.Units.smallSpacing

    Component.onCompleted: leaf._recalcOperatorWidth()

    // Non-visual measurer for _recalcOperatorWidth(). Matches the operator
    // combo's font so the cached widths are pixel-accurate; re-measures if the
    // font changes (e.g. a theme/scale switch).
    TextMetrics {
        id: opMetrics

        font: opCombo.font
        onFontChanged: leaf._recalcOperatorWidth()
    }

    Kirigami.Icon {
        id: fieldInfoIcon

        source: "dialog-information"
        Layout.preferredWidth: Kirigami.Units.iconSizes.small
        Layout.preferredHeight: Kirigami.Units.iconSizes.small
        // Top-align every control in the row so they line up with the value
        // field's top. The value editor can grow tall (a wrapped operator hint
        // sits beneath the field); vertically centring the siblings against that
        // taller column would drop the combos and trash below the field. The icon
        // is shorter than the field controls, so nudge it down to sit centred on
        // the first control line rather than at its very top.
        Layout.alignment: Qt.AlignTop
        Layout.topMargin: Math.max(0, (fieldCombo.height - height) / 2)
        color: Kirigami.Theme.highlightColor
        // Hover help describing the selected field. An unknown / legacy field
        // (_fieldEntry undefined) yields no text, so the tooltip just doesn't
        // appear rather than showing an empty bubble.
        readonly property string _fieldDesc: leaf._fieldEntry !== undefined ? (leaf._fieldEntry.description || "") : ""
        Accessible.name: _fieldDesc
        ToolTip.text: _fieldDesc
        ToolTip.visible: fieldInfoHover.hovered && _fieldDesc !== ""
        ToolTip.delay: Kirigami.Units.toolTipDelay

        HoverHandler {
            id: fieldInfoHover
        }
    }

    // Categorized field picker — the same cascading category-menu button the
    // shader choosers use (PZCommon.CategoryMenuButton). The field list is long,
    // so it's grouped into Identity / State / Size / Context. Keyed on the
    // field's `wire` string, matching `leaf.node.field`.
    PZCommon.CategoryMenuButton {
        id: fieldCombo

        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: Kirigami.Units.gridUnit * 9
        // Map the field metadata to the picker's { id, name, category,
        // categoryOrder } item shape.
        items: leaf.fieldOptions.map(function (o) {
            return {
                "id": o.wire,
                "name": o.label,
                "category": o.category,
                "categoryOrder": o.categoryOrder
            };
        })
        currentId: leaf.node.field
        placeholderText: i18n("Choose…")
        Accessible.description: i18n("Match field")
        onSelected: function (value) {
            if (value === leaf.node.field)
                return;

            // Changing the field invalidates the carried-over value and
            // (often) the carried-over operator. Concrete examples:
            //   appId "firefox" + field→isFullscreen → predicate
            //   `isFullscreen == "firefox"` (nonsense).
            //   appId + AppIdMatches + field→title → AppIdMatches is not a
            //   valid operator for Title, leaving the rule un-saveable.
            // So we reset the value to empty and only carry the operator
            // if the new field's allowed-operator set still includes it.
            var newFieldEntry = leaf._entryForWire(leaf.fieldOptions, value);
            var newOps = newFieldEntry !== undefined ? leaf.controller.operatorsForField(newFieldEntry.value) : [];
            var carryOp = leaf.node.op;
            var opStillValid = false;
            for (var i = 0; i < newOps.length; ++i) {
                if (newOps[i].wire === carryOp) {
                    opStillValid = true;
                    break;
                }
            }
            if (!opStillValid)
                carryOp = newOps.length > 0 ? newOps[0].wire : "";

            // Seed a value of the new field's kind so the leaf is immediately
            // well-typed rather than carrying an empty string a bool/number
            // field would coerce: bool -> false, number -> 0, everything else
            // (string / screen / activity / windowType / virtualDesktop picker)
            // -> "". The picker kinds (including virtualDesktop) treat "" as the
            // "no value yet" state and surface their placeholder until the user
            // picks, mirroring the screen / activity editors.
            var newKind = newFieldEntry !== undefined ? newFieldEntry.valueKind : "string";
            var seedValue = newKind === "bool" ? false : (newKind === "number" ? 0 : "");
            leaf._emit(value, carryOp, seedValue);
        }
    }

    WideComboBox {
        id: opCombo

        // Hidden until a field is chosen — a field-less placeholder has no
        // operator vocabulary to offer yet.
        visible: leaf._hasField
        Layout.alignment: Qt.AlignTop
        // Fixed to the widest operator label across ALL fields' operator sets
        // (plus chrome for the dropdown indicator + padding) instead of the
        // WideComboBox auto-width, which keys off the CURRENT field's operator
        // subset and so renders a different width per field (e.g. Desktop's
        // numeric operators vs Monitor's string operators). The gridUnit * 3
        // chrome allowance mirrors WideComboBox's own popup-width formula.
        Layout.preferredWidth: leaf._widestOperatorTextWidth + Kirigami.Units.gridUnit * 3
        textRole: "label"
        valueRole: "wire"
        model: leaf._operatorOptions
        currentIndex: leaf._indexForWire(leaf._operatorOptions, leaf.node.op)
        Accessible.name: i18n("Match operator")
        onActivated: function (index) {
            if (currentValue !== leaf.node.op)
                leaf._emit(leaf.node.field, currentValue, leaf.node.value);
        }
    }

    Loader {
        Layout.fillWidth: true
        // Hidden until a field is chosen — there is no value to type against a
        // field-less placeholder (mirrors the operator combo above).
        visible: leaf._hasField
        active: leaf._hasField
        // Most value editors top-align so a string editor's wrapped operator hint
        // can grow downward without dragging the combos with it. The bool toggle
        // is a short fixed-height control with no hint line, so it vertically
        // centers against the taller field / operator combos instead of clinging
        // to their top edge (which read as "not centered").
        Layout.alignment: leaf._valueKind === "bool" ? Qt.AlignVCenter : Qt.AlignTop
        sourceComponent: {
            if (leaf._valueKind === "bool")
                return boolValueEditor;

            if (leaf._valueKind === "number")
                return numberValueEditor;

            if (leaf._valueKind === "virtualDesktop")
                return virtualDesktopValueEditor;

            if (leaf._valueKind === "screen")
                return screenValueEditor;

            if (leaf._valueKind === "activity")
                return activityValueEditor;

            if (leaf._valueKind === "windowType")
                return windowTypeValueEditor;

            if (leaf._valueKind === "mode")
                return modeValueEditor;

            if (leaf._valueKind === "orientation")
                return orientationValueEditor;

            if (leaf._valueKind === "layout")
                return layoutValueEditor;

            return stringValueEditor;
        }
    }

    ToolButton {
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignTop
        ToolTip.text: i18n("Remove condition")
        ToolTip.visible: hovered
        Accessible.name: i18n("Remove this condition")
        onClicked: leaf.removeRequested()
    }

    Component {
        id: stringValueEditor

        // ColumnLayout root so an operator-specific input hint (regex syntax /
        // app-id-match semantics) can sit beneath the freeform field. The inner
        // RowLayout keeps the "pick from running windows" button next to the
        // field — that button only appears for fields where a live-window lookup
        // makes sense (typing an AppId / windowClass from memory is the friction
        // the picker existed to solve).
        ColumnLayout {
            id: stringEditorRoot

            spacing: Kirigami.Units.smallSpacing / 2

            // Operator-keyed input hint (regex / app-id match); empty for
            // operators that need none. Re-reads when the operator changes.
            readonly property string _valueHint: leaf.controller ? leaf.controller.matchValueHint(leaf.node.op) : ""

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                TextField {
                    Accessible.name: i18n("Match value")
                    Layout.fillWidth: true
                    placeholderText: i18n("Value")
                    text: leaf.node.value !== undefined ? String(leaf.node.value) : ""
                    onEditingFinished: leaf._emit(leaf.node.field, leaf.node.op, text)
                }

                ToolButton {
                    Accessible.name: leaf._pickTooltip(leaf.node.field)
                    Layout.alignment: Qt.AlignVCenter
                    ToolTip.delay: Kirigami.Units.toolTipDelay
                    ToolTip.text: leaf._pickTooltip(leaf.node.field)
                    ToolTip.visible: hovered
                    icon.name: "window-duplicate"
                    visible: leaf._fieldIsPickable(leaf.node.field)
                    onClicked: leaf._openWindowPicker()
                }
            }

            // Muted helper line under the value field; only present when the
            // current operator carries a hint. Plain text, word-wrapped, never
            // interactive. Mirrors the action-param hint in ActionRow.
            Label {
                Layout.fillWidth: true
                visible: stringEditorRoot._valueHint.length > 0
                text: stringEditorRoot._valueHint
                font: Kirigami.Theme.smallFont
                color: Kirigami.Theme.disabledTextColor
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                Accessible.ignored: true
            }
        }
    }

    Component {
        id: numberValueEditor

        SpinBox {
            // Numeric fields include Pid; the kernel pid_max ceiling is
            // 4194304, so 5e6 covers every possible PID with headroom. It is
            // written in scientific form because qmlformat rewrites large
            // decimal literals to 6-significant-figure scientific notation —
            // 5e6 is exact under that rounding (a plain 2147483647 is not).
            from: 0
            to: 5e+06
            value: Number(leaf.node.value) || 0
            Accessible.name: i18n("Match value")
            onValueModified: leaf._emit(leaf.node.field, leaf.node.op, value)
        }
    }

    Component {
        id: boolValueEditor

        // SettingsSwitch is a fixed-size custom Item whose track fills its
        // bounds, but the hosting Loader is Layout.fillWidth (for the text /
        // combo editors), which would stretch the toggle into a full-width pill.
        // Host it in a fill wrapper and keep the toggle at its implicit size,
        // left- and vertically-centered — the same control and wrapper the
        // action editor's bool param uses (ActionRow's _boolParamEditor), so
        // a WHEN bool value and a THEN bool value share one widget.
        Item {
            implicitHeight: boolToggle.implicitHeight

            SettingsSwitch {
                id: boolToggle

                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                checked: leaf.node.value === true
                accessibleName: i18n("Match value")
                // Echo the toggle's state as On / Off so the predicate value is
                // legible at a glance (the field and operator to the left say
                // what is being matched, this says to what). Same On / Off
                // vocabulary as the read-only match summary.
                label: checked ? i18n("On") : i18n("Off")
                onToggled: function (newValue) {
                    leaf._emit(leaf.node.field, leaf.node.op, newValue);
                }
            }
        }
    }

    // Virtual-desktop picker for the VirtualDesktop condition. The wire value stays
    // the 1-based desktop NUMBER (so the operator semantics — equals / greater /
    // less — are unchanged); the user just picks "2: Work" instead of typing a
    // number. Mirrors the screen picker, and the RouteToDesktop action editor.
    Component {
        id: virtualDesktopValueEditor

        WideComboBox {
            id: desktopCombo

            readonly property int _count: leaf.appSettings && leaf.appSettings.virtualDesktopCount > 0 ? leaf.appSettings.virtualDesktopCount : 1
            readonly property var _names: leaf.appSettings ? leaf.appSettings.virtualDesktopNames : []
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
                var target = Number(leaf.node.value);
                for (var i = 0; i < desktopCombo.model.length; ++i) {
                    if (desktopCombo.model[i].value === target)
                        return i;
                }
                return -1;
            }
            // A rule may reference a desktop beyond the current count (the desktop
            // was removed). Surface the stored number so the rule stays legible.
            displayText: currentIndex >= 0 ? currentText : (leaf.node.value ? ("" + leaf.node.value) : i18n("Choose a desktop…"))
            Accessible.name: i18n("Desktop")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }

    Component {
        id: screenValueEditor

        WideComboBox {
            id: screenCombo

            // Drive the picker off `appSettings.screens` so the user picks
            // "LG Ultra HD · DP-2" while the wire value remains the raw
            // connector / virtual-screen id. displayLabel already carries the
            // connector; append a Primary marker here (a plain ComboBox has no
            // badge surface like the monitor tiles do) so the user can tell the
            // primary monitor and which port each entry is on.
            readonly property var _screens: leaf.appSettings ? leaf.appSettings.screens : []
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
                var target = leaf.node.value;
                for (var i = 0; i < screenCombo._screens.length; ++i) {
                    if (screenCombo._screens[i].name === target)
                        return i;
                }
                return -1;
            }
            // The rule may reference a screen that isn't currently connected
            // (e.g. an offline monitor or a virtual-screen id whose physical
            // monitor is unplugged). Surface the stored id so the user can
            // tell what the rule pins to instead of an empty dropdown.
            displayText: currentIndex >= 0 ? currentText : (leaf.node.value || i18n("Choose a monitor…"))
            Accessible.name: i18n("Monitor")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }

    Component {
        id: activityValueEditor

        WideComboBox {
            // Picker over `appSettings.activities`; the wire value stays the
            // activity UUID so the rule store format is unchanged.
            model: leaf.appSettings ? leaf.appSettings.activities : []
            textRole: "name"
            valueRole: "id"
            currentIndex: {
                if (!leaf.appSettings)
                    return -1;

                var target = leaf.node.value;
                var list = leaf.appSettings.activities;
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].id === target)
                        return i;
                }
                return -1;
            }
            // Fall back to the raw activity id (e.g. an activity that has
            // been removed) so the rule's pin is visible even when no
            // current activity matches.
            displayText: currentIndex >= 0 ? currentText : (leaf.node.value || i18n("Choose an activity…"))
            Accessible.name: i18n("Activity")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }

    Component {
        id: windowTypeValueEditor

        WideComboBox {
            // The field entry's `options` carry `{value: int, wire: token,
            // label: localised}` triples — the wire value persisted in the
            // rule store is the underlying int of the PhosphorProtocol::
            // WindowType enum.
            readonly property var _options: leaf._fieldEntry !== undefined ? (leaf._fieldEntry.options || []) : []

            model: _options
            textRole: "label"
            valueRole: "value"
            currentIndex: {
                var target = leaf.node.value;
                for (var i = 0; i < _options.length; ++i) {
                    if (_options[i].value === target)
                        return i;
                }
                return -1;
            }
            // Show the raw stored value (e.g. an out-of-range int from a
            // hand-edited rule or a newer schema version) when no option
            // matches, mirroring the screen / activity pickers above. The
            // empty-string sentinel is the "no value yet" state the field
            // picker's onSelected seeds for picker-kind fields (windowType /
            // screen / activity) when the user switches in from another field —
            // treat it as no-value so the placeholder shows. Plain `||` would also
            // map int 0 (WindowType::Unknown) to the placeholder when no
            // option matched it, but that path is unreachable: 0 always
            // matches the Unknown option so currentIndex >= 0 and the
            // fallback never runs.
            displayText: {
                if (currentIndex >= 0)
                    return currentText;
                var v = leaf.node.value;
                if (v === undefined || v === null || v === "")
                    return i18n("Choose a window type…");
                return String(v);
            }
            Accessible.name: i18n("Window type")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }

    Component {
        id: modeValueEditor

        WideComboBox {
            // The field entry's `options` carry {value: token, wire: token,
            // label: localised} triples. Mode is a string field, so the value
            // persisted in the rule store IS the wire token ("snapping" /
            // "tiling"; Floating was dropped from the Mode condition).
            readonly property var _options: leaf._fieldEntry !== undefined ? (leaf._fieldEntry.options || []) : []

            model: _options
            textRole: "label"
            valueRole: "value"
            currentIndex: {
                var target = leaf.node.value;
                for (var i = 0; i < _options.length; ++i) {
                    if (_options[i].value === target)
                        return i;
                }
                return -1;
            }
            // Show the raw stored token when no option matches (a hand-edited
            // rule or a newer schema), mirroring the windowType / screen pickers.
            // The empty-string sentinel is the "no value yet" state the field
            // picker seeds when switching in from another field.
            displayText: {
                if (currentIndex >= 0)
                    return currentText;
                var v = leaf.node.value;
                if (v === undefined || v === null || v === "")
                    return i18n("Choose a mode…");
                return String(v);
            }
            Accessible.name: i18n("Placement mode")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }

    Component {
        id: orientationValueEditor

        WideComboBox {
            // Screen orientation is a string field whose value IS the wire token
            // ("portrait" / "landscape"); the options carry {value, wire, label}
            // triples, same shape as the mode editor above.
            readonly property var _options: leaf._fieldEntry !== undefined ? (leaf._fieldEntry.options || []) : []

            model: _options
            textRole: "label"
            valueRole: "value"
            currentIndex: {
                var target = leaf.node.value;
                for (var i = 0; i < _options.length; ++i) {
                    if (_options[i].value === target)
                        return i;
                }
                return -1;
            }
            displayText: {
                if (currentIndex >= 0)
                    return currentText;
                var v = leaf.node.value;
                if (v === undefined || v === null || v === "")
                    return i18n("Choose an orientation…");
                return String(v);
            }
            Accessible.name: i18n("Screen orientation")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }

    Component {
        id: layoutValueEditor

        WideComboBox {
            id: layoutCombo

            // Picker over `appSettings.layouts` (snapping layouts and autotile
            // entries); the wire value stays the layout id (snap UUID or
            // "autotile:<algo>") so it matches the id the daemon resolves for the
            // screen. Mirrors the activity picker.
            readonly property var _layouts: leaf.appSettings ? leaf.appSettings.layouts : []

            model: _layouts
            textRole: "displayName"
            valueRole: "id"
            currentIndex: {
                var target = leaf.node.value;
                for (var i = 0; i < layoutCombo._layouts.length; ++i) {
                    if (layoutCombo._layouts[i].id === target)
                        return i;
                }
                return -1;
            }
            // Fall back to the raw id (e.g. a deleted layout) so the rule's pin
            // stays visible even when no current layout matches.
            displayText: currentIndex >= 0 ? currentText : (leaf.node.value || i18n("Choose a layout…"))
            Accessible.name: i18n("Active layout")
            onActivated: function (index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);
            }
        }
    }
}
