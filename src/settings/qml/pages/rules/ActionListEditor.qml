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
    /// The RuleController — drives `defaultPayloadFor` so a freshly
    /// appended action and a type-switched action share the same seeding
    /// path. Forwarded to every ActionRow.
    required property var controller
    /// Registered action types from `RuleController.actionTypes()`.
    required property var actionTypeOptions
    /// The SettingsController — threaded into ActionRow so the picker-kind
    /// params (snappingLayout / tilingAlgorithm) can populate their dropdowns
    /// from `settingsController.layouts`.
    required property var appSettings
    /// True when the current match expression references only context fields.
    /// When false, every context-domain action (SetEngineMode, layout/tiling,
    /// DisableEngine) silently never fires — so the type-picker dims those
    /// entries with a warning tooltip. Threaded through to every ActionRow.
    required property bool matchIsContextOnly

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

    function _append() {
        // Append a type-less placeholder so the user must explicitly choose
        // the action from the picker. Auto-seeding a concrete type (the first
        // compatible entry) made every new action silently default to — and
        // look like — that type. The row renders just the "Choose…" picker
        // until a type is picked; ActionRow's onSelected then seeds that
        // type's param defaults via the controller's `defaultPayloadFor`
        // (the same path a type switch uses). `canSave` gates on every action
        // carrying a type, so an unfilled placeholder can't be saved.
        var next = editor.actions.slice();
        next.push({
            "type": ""
        });
        editor.actionsEdited(next);
    }

    spacing: Kirigami.Units.smallSpacing

    Label {
        text: i18n("THEN")
        font.bold: true
        opacity: 0.7
        font.capitalization: Font.AllUppercase
    }

    // Drive the row list off the action COUNT (an int), not the `actions`
    // array itself. Every param edit round-trips through `_replaceAt`, which
    // `.slice()`s the array and emits a NEW array reference; binding the
    // Repeater `model` to that var array makes Qt reset the model and rebuild
    // EVERY ActionRow on each keystroke — destroying and recreating the focused
    // editor (e.g. an opacity / animation-duration SpinBox) so it loses focus
    // and the user's input every time. An int `length` model only changes when
    // rows are added/removed, so a value edit re-binds each row's `action` in
    // place without recreating the delegate. This mirrors MatchExpressionEditor,
    // whose child Repeater keys off `_children.length` for the same reason.
    Repeater {
        model: editor.actions.length

        ActionRow {
            required property int index

            Layout.fillWidth: true
            action: editor.actions[index]
            controller: editor.controller
            actionTypeOptions: editor.actionTypeOptions
            appSettings: editor.appSettings
            matchIsContextOnly: editor.matchIsContextOnly
            onActionEdited: function (updated) {
                editor._replaceAt(index, updated);
            }
            onRemoveRequested: editor._removeAt(index)
        }
    }

    Label {
        visible: editor.actions.length === 0
        text: i18n("No actions yet. Add at least one.")
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
