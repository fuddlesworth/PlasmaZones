// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief OverlaySheet wrapper around `RuleEditorBody` for **editing** an
 *        existing rule.
 *
 * The body owns the actual editing surface — see RuleEditorBody.qml. The
 * sheet just hosts it, pins the `RuleEditorStatusBar` + Cancel/Save row to
 * its footer (so a tall rule's body scroll doesn't push the action buttons
 * off-screen), and emits `ruleSaved` when the user commits.
 *
 * Creation flows through `AddRuleWizard` instead — the two share the body
 * so the editing surface stays defined exactly once.
 */
Kirigami.OverlaySheet {
    id: sheet

    /// The WindowRuleController — passed through to the body.
    required property var controller
    /// The SettingsController bridge — passed through to the body.
    required property var appSettings
    /// Working copy of the rule being edited. Set via `openFor`. The body
    /// mutates it in place; Cancel discards by simply closing the sheet
    /// (the staged rule never reaches the controller).
    property var _workingRule: ({
    })

    signal ruleSaved(var ruleJson)

    /// Open the sheet to edit @p ruleJson. `editing` is no longer a flag —
    /// the only callers of this sheet edit an existing rule (the row's
    /// pencil button); creation routes through AddRuleWizard.
    function openFor(ruleJson) {
        // Deep clone so Cancel can't leak mutations back to the model.
        sheet._workingRule = JSON.parse(JSON.stringify(ruleJson));
        sheet.open();
    }

    title: i18n("Edit Window Rule")

    RuleEditorBody {
        id: editorBody

        controller: sheet.controller
        appSettings: sheet.appSettings
        workingRule: sheet._workingRule
        onWorkingRuleChanged: {
            // Echo body edits back into the sheet's mirror so Save reads the
            // latest state. The body owns mutation; this property lives on
            // the sheet only to keep the openFor seed survival path explicit.
            sheet._workingRule = workingRule;
        }
    }

    footer: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RuleEditorStatusBar {
            Layout.fillWidth: true
            canSave: editorBody.canSave
            validationIssues: editorBody.validationIssues
            workingRule: editorBody.workingRule
        }

        RowLayout {
            Layout.fillWidth: true

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: i18n("Cancel")
                Accessible.name: i18n("Cancel and discard changes")
                onClicked: sheet.close()
            }

            Button {
                text: i18n("Save")
                icon.name: "dialog-ok-apply"
                enabled: editorBody.canSave
                Accessible.name: i18n("Save changes to this rule")
                onClicked: {
                    sheet.ruleSaved(sheet._workingRule);
                    sheet.close();
                }
            }

        }

    }

}
