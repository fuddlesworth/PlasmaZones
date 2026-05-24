// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The "THEN" block — the editable action list of a rule.
 *
 * Holds a JS array of action JSON objects. Edits emit `actionsEdited(array)`;
 * the parent (RuleEditorSheet) owns the working rule.
 */
ColumnLayout {
    id: editor

    /// JS array of action objects — `[{ type, ...params }, ...]`.
    required property var actions
    /// Registered action types from `WindowRuleController.actionTypes()`.
    required property var actionTypeOptions
    /// The SettingsController — threaded into ActionRow so the picker-kind
    /// params (snappingLayout / tilingAlgorithm) can populate their dropdowns
    /// from `settingsController.layouts`.
    required property var appSettings

    signal actionsEdited(var updatedActions)

    function _replaceAt(index, updated) {
        var next = editor.actions.slice();
        next[index] = updated;
        editor.actionsEdited(next);
    }

    function _removeAt(index) {
        var next = editor.actions.slice();
        next.splice(index, 1);
        editor.actionsEdited(next);
    }

    /// A valid starting value for a parameter descriptor — driven entirely off
    /// its `kind`, so the per-kind defaulting lives in exactly one place.
    function _defaultParamValue(param) {
        if (param.kind === "enum") {
            // Enum options now carry `{value, label}` pairs — the wire form
            // stored in the action is the `value`. Older callers passed plain
            // strings; tolerate both shapes so a future free-form enum doesn't
            // need to coordinate the schema change.
            if (!param.options || param.options.length === 0)
                return "";

            var first = param.options[0];
            return (first && typeof first === "object") ? (first.value || "") : first;
        }
        if (param.kind === "number" || param.kind === "percent")
            return param.min !== undefined ? param.min : 0;

        return "";
    }

    function _append() {
        // Guarded by the Add-action button's enabled state — there is always
        // at least one registered type when this runs. Pre-seed every param
        // declared by the type's descriptor so a freshly-added action carries
        // a complete (if not yet user-filled) param set.
        var typeEntry = editor.actionTypeOptions[0];
        var action = {
            "type": typeEntry.value
        };
        var params = typeEntry.params || [];
        for (var i = 0; i < params.length; ++i) action[params[i].key] = editor._defaultParamValue(params[i])
        var next = editor.actions.slice();
        next.push(action);
        editor.actionsEdited(next);
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
            appSettings: editor.appSettings
            onActionEdited: function(updated) {
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
