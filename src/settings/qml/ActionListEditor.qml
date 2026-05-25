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
    /// The WindowRuleController — drives `defaultPayloadFor` so a freshly
    /// appended action and a type-switched action share the same seeding
    /// path. Forwarded to every ActionRow.
    required property var controller
    /// Registered action types from `WindowRuleController.actionTypes()`.
    required property var actionTypeOptions
    /// The SettingsController — threaded into ActionRow so the picker-kind
    /// params (snappingLayout / tilingAlgorithm) can populate their dropdowns
    /// from `settingsController.layouts`.
    required property var appSettings
    /// True when the current match expression references only context fields.
    /// When false, every context-domain action (SetEngineMode, layout/tiling,
    /// DisableEngine) silently never fires — so the type-picker disables those
    /// entries with a tooltip and the default "Add action" picks the first
    /// **compatible** entry instead of always index 0.
    required property bool matchIsContextOnly
    /// First entry in `actionTypeOptions` whose domain is compatible with the
    /// current match. Used as the default for a freshly-added action so the
    /// user is not handed an incompatible type by default. Falls back to the
    /// first entry when nothing is compatible — that case is impossible today
    /// (window-domain actions are always compatible) but the fallback keeps
    /// the picker non-empty if the future schema ever introduces a domain
    /// that is constrained both ways.
    readonly property var _firstCompatibleType: {
        for (var i = 0; i < editor.actionTypeOptions.length; ++i) {
            var entry = editor.actionTypeOptions[i];
            if (entry.domain !== "context" || editor.matchIsContextOnly)
                return entry;

        }
        return editor.actionTypeOptions.length > 0 ? editor.actionTypeOptions[0] : undefined;
    }

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
        // Guarded by the Add-action button's enabled state — there is always
        // at least one registered type when this runs. Pre-seed every param
        // declared by the type's descriptor via the controller's
        // `defaultPayloadFor` so a freshly-added action carries a complete
        // (if not yet user-filled) param set. The default type is the first
        // **compatible** entry so a window-property match doesn't auto-stamp
        // a context action the picker would then flag as invalid. Defaults
        // live in the controller (not duplicated here) so a type switch in
        // ActionRow and a fresh append hit the same seeding path — adding
        // a new param kind only requires updating the C++ default map.
        var typeEntry = editor._firstCompatibleType || editor.actionTypeOptions[0];
        var action = editor.controller.defaultPayloadFor(typeEntry.value);
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
            controller: editor.controller
            actionTypeOptions: editor.actionTypeOptions
            appSettings: editor.appSettings
            matchIsContextOnly: editor.matchIsContextOnly
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
