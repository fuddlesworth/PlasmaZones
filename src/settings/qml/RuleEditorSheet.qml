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
 *
 * Close gating: every close path (Cancel button, the Esc key, the X close
 * button) routes through `_requestClose()`. If the working rule diverges
 * from the snapshot captured in `openFor`, the user is prompted via the
 * shared `DiscardChangesDialog`; only a "Discard" confirmation actually
 * closes the sheet. This is the only safeguard against a stray Esc or
 * misclick wiping out minutes of authoring work on a multi-leaf composite.
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
    property var _workingRule: ({})
    /// JSON snapshot taken at `openFor` time so the close paths can detect
    /// whether the user has made any edits. Compared by serialised string
    /// equality — the working rule is plain JSON, so this is deterministic
    /// and order-stable (parsed from / written to the same `JSON.parse`
    /// output, so key order is preserved).
    property string _initialSnapshot: ""

    signal ruleSaved(var ruleJson)

    /// Open the sheet to edit @p ruleJson. `editing` is no longer a flag —
    /// the only callers of this sheet edit an existing rule (the row's
    /// pencil button); creation routes through AddRuleWizard.
    function openFor(ruleJson) {
        // Deep clone so Cancel can't leak mutations back to the model.
        const cloned = JSON.parse(JSON.stringify(ruleJson));
        sheet._workingRule = cloned;
        // Stash the post-clone string so a re-serialise of the working rule
        // can be string-compared without worrying about key-order drift —
        // both sides go through the same JSON.stringify call shape.
        sheet._initialSnapshot = JSON.stringify(cloned);
        sheet.open();
    }

    /// Route every close request (Cancel button, Esc shortcut, X close
    /// button) through this gate so the dirty check is honoured uniformly.
    /// A clean working rule closes immediately; a dirty one opens the
    /// shared confirm dialog, and only its "Discard" path closes the sheet.
    ///
    /// Dirty check reads `editorBody.workingRule` (the body's live mutated
    /// copy) rather than `sheet._workingRule` (the openFor seed). The
    /// body owns mutation; the previous shape echoed body changes back
    /// into `_workingRule`, which produced a binding loop under Qt 6.11.
    function _requestClose() {
        if (JSON.stringify(editorBody.workingRule) === sheet._initialSnapshot) {
            sheet.close();
            return;
        }
        discardConfirm.open();
    }

    title: i18n("Edit Window Rule")
    // The built-in X close button bypasses our intercept (it calls
    // `root.close()` directly inside OverlaySheet). Hide it and let the
    // explicit Cancel button + Esc shortcut be the only close paths so the
    // dirty check cannot be silently sidestepped.
    showCloseButton: false
    // `CloseOnEscape` is the OverlaySheet default; replace it with
    // NoAutoClose so the Esc key routes through `_requestClose()` via our
    // own Shortcut instead of slamming the sheet shut.
    closePolicy: Popup.NoAutoClose

    Shortcut {
        // `sequences` (plural) binds to all key sequences associated
        // with StandardKey.Cancel — Qt warns about only catching one
        // of them when the singular form is used.
        sequences: [StandardKey.Cancel]
        enabled: sheet.opened
        onActivated: sheet._requestClose()
    }

    DiscardChangesDialog {
        id: discardConfirm

        onDiscardConfirmed: sheet.close()
    }

    RuleEditorBody {
        id: editorBody

        controller: sheet.controller
        appSettings: sheet.appSettings
        // Seed once via openFor → sheet._workingRule. The body owns
        // mutation thereafter. NO echo back into _workingRule — that
        // produced a Qt 6.11 binding loop. Consumers (dirty check,
        // Save) read editorBody.workingRule directly.
        workingRule: sheet._workingRule
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
                onClicked: sheet._requestClose()
            }

            Button {
                text: i18n("Save")
                icon.name: "dialog-ok-apply"
                enabled: editorBody.canSave
                Accessible.name: i18n("Save changes to this rule")
                onClicked: {
                    // Read body's live working rule (not the seed) —
                    // see the binding-loop comment on editorBody above.
                    sheet.ruleSaved(editorBody.workingRule);
                    sheet.close();
                }
            }
        }
    }
}
