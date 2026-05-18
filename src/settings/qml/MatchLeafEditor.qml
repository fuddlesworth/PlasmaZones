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
    /// The WindowRuleController (for matchFields / operatorsForField).
    required property var controller
    readonly property var _fieldOptions: controller.matchFields()
    // Operator options depend on the current field's enum value.
    readonly property int _fieldEnum: leaf._enumForFieldName(leaf.node.field)
    readonly property var _operatorOptions: controller.operatorsForField(leaf._fieldEnum)
    readonly property string _valueKind: leaf._valueKindForField(leaf.node.field)
    // Wire-string ↔ Field-enum table. Mirrors PhosphorWindowRule::Field.
    readonly property var _fieldNames: ["appId", "windowClass", "desktopFile", "windowRole", "pid", "title", "windowType", "isSticky", "isFullscreen", "isMinimized", "screenId", "virtualDesktop", "activity"]
    // Wire-string ↔ Operator-enum table. Mirrors PhosphorWindowRule::Operator.
    readonly property var _operatorNames: ["equals", "contains", "startsWith", "endsWith", "regex", "appIdMatches", "in", "greaterThan", "lessThan"]

    signal leafChanged(var updatedLeaf)
    signal removeRequested()

    function _enumForFieldName(name) {
        for (var i = 0; i < leaf._fieldOptions.length; ++i) {
            // matchFields() carries the wire string under no key — match the
            // controller's enum by recomputing from the Field order. We map
            // via the label-independent index: WindowRuleController emits
            // entries in Field enum order, so value === enum int.
            if (leaf._fieldNameForEnum(leaf._fieldOptions[i].value) === name)
                return leaf._fieldOptions[i].value;

        }
        return leaf._fieldOptions.length > 0 ? leaf._fieldOptions[0].value : 0;
    }

    function _fieldNameForEnum(e) {
        return (e >= 0 && e < leaf._fieldNames.length) ? leaf._fieldNames[e] : "appId";
    }

    function _valueKindForField(name) {
        for (var i = 0; i < leaf._fieldOptions.length; ++i) {
            if (leaf._fieldNameForEnum(leaf._fieldOptions[i].value) === name)
                return leaf._fieldOptions[i].valueKind;

        }
        return "string";
    }

    function _operatorNameForEnum(e) {
        return (e >= 0 && e < leaf._operatorNames.length) ? leaf._operatorNames[e] : "equals";
    }

    function _fieldIndex() {
        for (var i = 0; i < leaf._fieldOptions.length; ++i) {
            if (leaf._fieldNameForEnum(leaf._fieldOptions[i].value) === leaf.node.field)
                return i;

        }
        return 0;
    }

    function _operatorIndex() {
        for (var i = 0; i < leaf._operatorOptions.length; ++i) {
            if (leaf._operatorNameForEnum(leaf._operatorOptions[i].value) === leaf.node.op)
                return i;

        }
        return 0;
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

    ComboBox {
        id: fieldCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 9
        textRole: "label"
        valueRole: "value"
        model: leaf._fieldOptions
        currentIndex: leaf._fieldIndex()
        Accessible.name: i18n("Match field")
        onActivated: function(index) {
            var name = leaf._fieldNameForEnum(currentValue);
            if (name !== leaf.node.field)
                leaf._emit(name, leaf.node.op, leaf.node.value);

        }
    }

    ComboBox {
        id: opCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 9
        textRole: "label"
        valueRole: "value"
        model: leaf._operatorOptions
        currentIndex: leaf._operatorIndex()
        Accessible.name: i18n("Match operator")
        onActivated: function(index) {
            var name = leaf._operatorNameForEnum(currentValue);
            if (name !== leaf.node.op)
                leaf._emit(leaf.node.field, name, leaf.node.value);

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
            from: 0
            to: 999999
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

}
