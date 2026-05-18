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
        // Guarded by the Add-action button's enabled state — there is always
        // at least one registered type when this runs.
        var next = editor.actions.slice();
        next.push({
            "type": editor.actionTypeOptions[0].value
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
        // No registered action types ⇒ nothing to add; disable rather than
        // appending a hardcoded fallback type.
        enabled: editor.actionTypeOptions.length > 0
        Accessible.name: i18n("Add an action to this rule")
        onClicked: editor._append()
    }

}
