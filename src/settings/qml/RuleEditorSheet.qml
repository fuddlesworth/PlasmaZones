// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The full rule editor — the "Advanced / Custom" editing surface.
 *
 * Edits one rule's name, enabled flag, priority, match-expression tree
 * (MatchExpressionEditor) and action list (ActionListEditor). Opened by
 * WindowRulesPage for an existing rule (edit) or by AddRuleSheet once the
 * user picks a subject (create).
 *
 * The sheet owns a working copy `_workingRule` so a Cancel discards cleanly;
 * Save emits `ruleSaved(ruleJson)` and the page commits it to the controller.
 */
Kirigami.OverlaySheet {
    id: sheet

    /// The WindowRuleController — threaded into the recursive editors.
    required property var controller
    /// True when editing an existing rule (vs creating a new one).
    property bool editing: false
    /// Working copy of the rule being edited. Set via `openFor`.
    property var _workingRule: ({
    })
    /// Stable empty-match fallback — a single allocation, so binding
    /// MatchExpressionEditor.node to it does not churn the node identity on
    /// every binding evaluation.
    readonly property var _emptyMatch: ({
        "all": []
    })
    /// The controller's authoring metadata, cached once. `actionTypes()` and
    /// `matchFields()` are Q_INVOKABLEs that allocate a fresh QVariantList on
    /// every call — binding them directly would re-invoke (and churn the
    /// dependent Repeaters) on every unrelated `_workingRule` patch. Cache
    /// here and thread the cached lists down to the child editors.
    readonly property var _actionTypeOptions: sheet.controller.actionTypes()
    readonly property var _matchFieldOptions: sheet.controller.matchFields()
    /// Save is allowed only when the rule has at least one action and every
    /// match leaf has a non-empty value.
    readonly property bool _canSave: sheet._workingRule.actions !== undefined && sheet._workingRule.actions.length > 0 && sheet._matchHasFilledLeaves(sheet._workingRule.match)

    signal ruleSaved(var ruleJson)

    /// Open the sheet to edit @p ruleJson (a JSON map). Pass `isEdit` false
    /// for a freshly-created rule.
    function openFor(ruleJson, isEdit) {
        // Deep-ish clone so Cancel can't leak mutations back to the model.
        sheet._workingRule = JSON.parse(JSON.stringify(ruleJson));
        sheet.editing = isEdit === true;
        sheet.open();
    }

    function _patch(key, value) {
        var next = JSON.parse(JSON.stringify(sheet._workingRule));
        next[key] = value;
        sheet._workingRule = next;
    }

    /// True if every leaf predicate in @p node carries a non-empty value.
    /// A leaf with an empty string / missing value would match an empty-string
    /// id (e.g. the guided `ScreenId == ""` seed) — block saving that.
    function _matchHasFilledLeaves(node) {
        if (!node)
            return true;

        // Leaf — `field` is the discriminator (mirrors MatchExpressionEditor).
        if (node.field !== undefined) {
            var v = node.value;
            if (v === undefined || v === null)
                return false;

            return String(v).length > 0;
        }
        // Composite — recurse into whichever child list is present.
        // An empty `all` is the legitimate catch-all and stays valid, but an
        // empty `any` (matches nothing) or empty `none` (matches everything)
        // is a degenerate group — block saving it.
        if (node.any !== undefined || node.none !== undefined) {
            var degenerate = node.any !== undefined ? node.any : node.none;
            if (!degenerate || degenerate.length === 0)
                return false;

        }
        var children = node.all || node.any || node.none || [];
        for (var i = 0; i < children.length; ++i) {
            if (!sheet._matchHasFilledLeaves(children[i]))
                return false;

        }
        return true;
    }

    title: sheet.editing ? i18n("Edit Window Rule") : i18n("New Window Rule")

    ColumnLayout {
        implicitWidth: Kirigami.Units.gridUnit * 36
        spacing: Kirigami.Units.largeSpacing

        // ── Identity ──
        Kirigami.FormLayout {
            Layout.fillWidth: true

            TextField {
                Kirigami.FormData.label: i18n("Name:")
                text: sheet._workingRule.name || ""
                Accessible.name: i18n("Rule name")
                onEditingFinished: sheet._patch("name", text)
            }

            Switch {
                Kirigami.FormData.label: i18n("Enabled:")
                checked: sheet._workingRule.enabled !== false
                Accessible.name: i18n("Rule enabled")
                onToggled: sheet._patch("enabled", checked)
            }

            SpinBox {
                Kirigami.FormData.label: i18n("Priority:")
                from: 0
                to: 100000
                value: sheet._workingRule.priority || 0
                Accessible.name: i18n("Rule priority — higher rules are evaluated first")
                onValueModified: sheet._patch("priority", value)
            }

        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── WHEN — the match-expression tree ──
        Label {
            text: i18n("WHEN")
            font.bold: true
            opacity: 0.7
            font.capitalization: Font.AllUppercase
        }

        MatchExpressionEditor {
            Layout.fillWidth: true
            node: sheet._workingRule.match || sheet._emptyMatch
            controller: sheet.controller
            matchFieldOptions: sheet._matchFieldOptions
            depth: 0
            removable: false
            onNodeChanged: function(updated) {
                sheet._patch("match", updated);
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── THEN — the action list ──
        ActionListEditor {
            Layout.fillWidth: true
            actions: sheet._workingRule.actions || []
            actionTypeOptions: sheet._actionTypeOptions
            onActionsChanged: function(updated) {
                sheet._patch("actions", updated);
            }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.italic: true
            opacity: 0.6
            text: i18n("The first rule (by priority) to fill each action slot wins that slot — actions in different slots stack.")
        }

        Kirigami.InlineMessage {
            Layout.fillWidth: true
            type: Kirigami.MessageType.Information
            visible: !sheet._canSave
            text: !sheet._workingRule.actions || sheet._workingRule.actions.length === 0 ? i18n("Add at least one action before saving.") : i18n("Every condition needs a value before this rule can be saved.")
        }

    }

    footer: RowLayout {
        Item {
            Layout.fillWidth: true
        }

        Button {
            text: i18n("Cancel")
            Accessible.name: i18n("Cancel and discard changes")
            onClicked: sheet.close()
        }

        Button {
            text: sheet.editing ? i18n("Save") : i18n("Add rule")
            icon.name: "dialog-ok-apply"
            // A rule with no actions does nothing, and a rule whose match has
            // an empty leaf value would match an empty-string id — block both.
            enabled: sheet._canSave
            Accessible.name: sheet.editing ? i18n("Save changes to this rule") : i18n("Add this rule")
            onClicked: {
                sheet.ruleSaved(sheet._workingRule);
                sheet.close();
            }
        }

    }

}
