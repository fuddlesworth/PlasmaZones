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
            node: sheet._workingRule.match || ({
                "all": []
            })
            controller: sheet.controller
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
            actionTypeOptions: sheet.controller.actionTypes()
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
            Accessible.name: sheet.editing ? i18n("Save changes to this rule") : i18n("Add this rule")
            onClicked: {
                sheet.ruleSaved(sheet._workingRule);
                sheet.close();
            }
        }

    }

}
