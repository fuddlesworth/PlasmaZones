// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One editable action row inside ActionListEditor.
 *
 * An action is a `{ type, ...params }` JSON object. The row exposes a type
 * dropdown and one editor per parameter, both driven entirely by the
 * `actionTypeOptions` metadata from `WindowRuleController.actionTypes()` —
 * there is no per-type `if (t === "...")` ladder here. Two-way: edits emit
 * `actionChanged(updatedAction)`; the parent owns the list.
 */
RowLayout {
    id: row

    /// The action JSON object being edited — `{ type, ...params }`.
    required property var action
    /// Registered action types from `WindowRuleController.actionTypes()` —
    /// each entry: `{ value, label, params: [{ key, kind, label, ... }] }`.
    required property var actionTypeOptions
    /// The descriptor for the current action's type, or undefined if unknown.
    readonly property var _typeEntry: row._entryForType(row.action.type)
    /// Parameter descriptors for the current type (empty when none / unknown).
    readonly property var _params: row._typeEntry !== undefined ? row._typeEntry.params : []

    signal actionChanged(var updatedAction)
    signal removeRequested()

    /// Shallow-clone the action so a mutation produces a fresh object QML
    /// rebinds against (mutating in place would not re-trigger bindings).
    function _withParam(key, value) {
        var next = {
        };
        for (var k in row.action) next[k] = row.action[k]
        next[key] = value;
        return next;
    }

    /// The actionTypeOptions descriptor whose `value` equals @p type.
    function _entryForType(type) {
        for (var i = 0; i < row.actionTypeOptions.length; ++i) {
            if (row.actionTypeOptions[i].value === type)
                return row.actionTypeOptions[i];

        }
        return undefined;
    }

    /// The index of the current action's type — -1 for an unknown / legacy
    /// type so the combo shows no selection rather than coercing to index 0.
    function _typeIndex() {
        for (var i = 0; i < row.actionTypeOptions.length; ++i) {
            if (row.actionTypeOptions[i].value === row.action.type)
                return i;

        }
        return -1;
    }

    spacing: Kirigami.Units.smallSpacing

    Kirigami.Icon {
        source: "arrow-right"
        Layout.preferredWidth: Kirigami.Units.iconSizes.small
        Layout.preferredHeight: Kirigami.Units.iconSizes.small
        Layout.alignment: Qt.AlignVCenter
    }

    ComboBox {
        id: typeCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 13
        textRole: "label"
        valueRole: "value"
        model: row.actionTypeOptions
        currentIndex: row._typeIndex()
        Accessible.name: i18n("Action type")
        onActivated: function(index) {
            if (currentValue !== row.action.type)
                row.actionChanged({
                "type": currentValue
            });

        }
    }

    // One editor per parameter — the shape comes from the param `kind`, never
    // an action-type ladder.
    Repeater {
        model: row._params

        delegate: Loader {
            required property var modelData

            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            sourceComponent: {
                if (modelData.kind === "enum")
                    return enumParamEditor;

                if (modelData.kind === "number" || modelData.kind === "percent")
                    return numberParamEditor;

                return stringParamEditor;
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

    Component {
        id: stringParamEditor

        TextField {
            required property var modelData

            text: row.action[modelData.key] !== undefined ? String(row.action[modelData.key]) : ""
            placeholderText: modelData.label
            Accessible.name: modelData.label
            onEditingFinished: row.actionChanged(row._withParam(modelData.key, text))
        }

    }

    Component {
        id: numberParamEditor

        SpinBox {
            required property var modelData
            // "percent" stores `display * scale`; "number" stores the raw value.
            readonly property real _scale: modelData.scale !== undefined ? modelData.scale : 1
            readonly property real _stored: row.action[modelData.key] !== undefined ? row.action[modelData.key] : 0

            from: modelData.min !== undefined ? modelData.min : 0
            to: modelData.max !== undefined ? modelData.max : 999999
            value: Math.round(_stored / _scale)
            Accessible.name: modelData.label
            onValueModified: row.actionChanged(row._withParam(modelData.key, value * _scale))
        }

    }

    Component {
        id: enumParamEditor

        ComboBox {
            required property var modelData

            model: modelData.options
            // -1 for an unknown stored value so it is not coerced to the
            // first option and committed over the user's data.
            currentIndex: modelData.options.indexOf(row.action[modelData.key])
            Accessible.name: modelData.label
            onActivated: row.actionChanged(row._withParam(modelData.key, currentText))
        }

    }

}
