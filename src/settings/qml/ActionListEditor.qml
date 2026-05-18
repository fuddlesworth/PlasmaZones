// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The "THEN" block — the editable action list of a rule.
 *
 * Holds a JS array of action JSON objects. Edits emit `actionsChanged(array)`;
 * the parent (RuleEditorSheet) owns the working rule.
 */
ColumnLayout {
    id: editor

    /// JS array of action objects — `[{ type, ...params }, ...]`.
    required property var actions
    /// Registered action types from `WindowRuleController.actionTypes()`.
    required property var actionTypeOptions

    signal actionsChanged(var updatedActions)

    function _replaceAt(index, updated) {
        var next = editor.actions.slice();
        next[index] = updated;
        editor.actionsChanged(next);
    }

    function _removeAt(index) {
        var next = editor.actions.slice();
        next.splice(index, 1);
        editor.actionsChanged(next);
    }

    function _append() {
        var next = editor.actions.slice();
        // Default to the first registered action type.
        next.push({
            "type": editor.actionTypeOptions.length > 0 ? editor.actionTypeOptions[0].value : "float"
        });
        editor.actionsChanged(next);
    }

    spacing: Kirigami.Units.smallSpacing

    Label {
        text: i18n("THEN")
        font.bold: true
        opacity: 0.7
        font.capitalization: Font.AllUppercase
    }

    Repeater {
        model: editor.actions

        ActionRow {
            required property int index
            required property var modelData

            Layout.fillWidth: true
            action: modelData
            actionTypeOptions: editor.actionTypeOptions
            onActionChanged: function(updated) {
                editor._replaceAt(index, updated);
            }
            onRemoveRequested: editor._removeAt(index)
        }

    }

    Label {
        visible: editor.actions.length === 0
        text: i18n("No actions yet — add at least one.")
        opacity: 0.6
        font.italic: true
    }

    Button {
        text: i18n("Add action")
        icon.name: "list-add"
        flat: true
        Accessible.name: i18n("Add an action to this rule")
        onClicked: editor._append()
    }

}
