// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One leaf predicate row — `{ field, op, value }`.
 *
 * Field and operator are dropdowns; the value field shape adapts to the
 * field's value kind (string / number / bool). Two-way: edits emit
 * `leafChanged(updatedLeaf)`.
 */
RowLayout {
    id: leaf

    /// The leaf JSON object — `{ field: "appId", op: "equals", value: ... }`.
    required property var node
    /// The WindowRuleController (for operatorsForField).
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
    // Operator options depend on the current field's enum value.
    readonly property var _operatorOptions: leaf._fieldEntry !== undefined ? controller.operatorsForField(leaf._fieldEntry.value) : []
    readonly property string _valueKind: leaf._fieldEntry !== undefined ? leaf._fieldEntry.valueKind : "string"

    signal leafChanged(var updatedLeaf)
    signal removeRequested()

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

    spacing: Kirigami.Units.smallSpacing

    Kirigami.Icon {
        source: "dialog-information"
        Layout.preferredWidth: Kirigami.Units.iconSizes.small
        Layout.preferredHeight: Kirigami.Units.iconSizes.small
        Layout.alignment: Qt.AlignVCenter
        color: Kirigami.Theme.highlightColor
    }

    // `WideComboBox` (not plain `ComboBox`) — sizes the popup to fit the
    // widest item so options like "starts with" / "is one of" don't truncate.
    // `implicitContentWidthPolicy: WidestTextWhenCompleted` (set on the
    // WideComboBox base) also widens the closed combo to fit the longest
    // current label, so we drop the fixed `Layout.preferredWidth` here.
    WideComboBox {
        id: fieldCombo

        textRole: "label"
        valueRole: "wire"
        model: leaf.fieldOptions
        // -1 for an unknown / legacy field — show no selection rather than
        // silently coercing it to the first field.
        currentIndex: leaf._indexForWire(leaf.fieldOptions, leaf.node.field)
        Accessible.name: i18n("Match field")
        onActivated: function(index) {
            if (currentValue !== leaf.node.field)
                leaf._emit(currentValue, leaf.node.op, leaf.node.value);

        }
    }

    WideComboBox {
        id: opCombo

        textRole: "label"
        valueRole: "wire"
        model: leaf._operatorOptions
        currentIndex: leaf._indexForWire(leaf._operatorOptions, leaf.node.op)
        Accessible.name: i18n("Match operator")
        onActivated: function(index) {
            if (currentValue !== leaf.node.op)
                leaf._emit(leaf.node.field, currentValue, leaf.node.value);

        }
    }

    Loader {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignVCenter
        sourceComponent: {
            if (leaf._valueKind === "bool")
                return boolValueEditor;

            if (leaf._valueKind === "number")
                return numberValueEditor;

            if (leaf._valueKind === "screen")
                return screenValueEditor;

            if (leaf._valueKind === "activity")
                return activityValueEditor;

            return stringValueEditor;
        }
    }

    ToolButton {
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: i18n("Remove condition")
        ToolTip.visible: hovered
        Accessible.name: i18n("Remove this condition")
        onClicked: leaf.removeRequested()
    }

    Component {
        id: stringValueEditor

        TextField {
            text: leaf.node.value !== undefined ? String(leaf.node.value) : ""
            placeholderText: i18n("Value")
            Accessible.name: i18n("Match value")
            onEditingFinished: leaf._emit(leaf.node.field, leaf.node.op, text)
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

        CheckBox {
            checked: leaf.node.value === true
            text: checked ? i18n("True") : i18n("False")
            Accessible.name: i18n("Match value")
            onToggled: leaf._emit(leaf.node.field, leaf.node.op, checked)
        }

    }

    Component {
        id: screenValueEditor

        WideComboBox {
            // Drive the picker off `appSettings.screens` so the user picks
            // "LG Ultra HD · DP-2" while the wire value remains the raw
            // connector / virtual-screen id.
            model: leaf.appSettings ? leaf.appSettings.screens : []
            textRole: "displayLabel"
            valueRole: "name"
            currentIndex: {
                if (!leaf.appSettings)
                    return -1;

                var target = leaf.node.value;
                var list = leaf.appSettings.screens;
                for (var i = 0; i < list.length; ++i) {
                    if (list[i].name === target)
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
            onActivated: function(index) {
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
            onActivated: function(index) {
                if (currentValue !== leaf.node.value)
                    leaf._emit(leaf.node.field, leaf.node.op, currentValue);

            }
        }

    }

}
