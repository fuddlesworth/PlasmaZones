// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Dialog wrapper around `RuleEditorBody` for **editing** an existing
 *        rule.
 *
 * The body owns the actual editing surface — see RuleEditorBody.qml. This
 * dialog hosts it, surfaces the `RuleEditorStatusBar`, exposes Cancel/Save in
 * the footer, and emits `ruleSaved` when the user commits.
 *
 * Uses `Kirigami.Dialog` (NOT `Kirigami.OverlaySheet`) deliberately: the same
 * container `AddRuleWizard` uses with this same body. OverlaySheet derives its
 * `implicitHeight` from the content while its own `y` depends on that
 * implicitHeight (OverlaySheet template :122/:139/:153); with a content tree
 * whose height shifts as it lays out, that self-reference never settles and Qt
 * reports a persistent "Binding loop detected for property implicitHeight".
 * Kirigami.Dialog pins its width via `preferredWidth` and wraps content in its
 * own ScrollView, so there is no content→size→content feedback — the wizard
 * proves this body renders loop-free inside a Dialog.
 *
 * Creation flows through `AddRuleWizard` instead — the two share the body so
 * the editing surface stays defined exactly once.
 *
 * Close gating: every close path (Cancel button, the Esc key) routes through
 * `_requestClose()`. If the working rule diverges from the snapshot captured in
 * `openFor`, the user is prompted via the shared `DiscardChangesDialog`; only a
 * "Discard" confirmation actually closes. This guards against a stray Esc or
 * misclick wiping out minutes of authoring work on a multi-leaf composite.
 */
Kirigami.Dialog {
    id: sheet

    /// The WindowRuleController — passed through to the body.
    required property var controller
    /// The SettingsController bridge — passed through to the body.
    required property var appSettings
    /// Working copy of the rule being edited. Set via `openFor`. The body
    /// mutates it in place; Cancel discards by simply closing the dialog
    /// (the staged rule never reaches the controller).
    property var _workingRule: ({})
    /// JSON snapshot taken at `openFor` time so the close paths can detect
    /// whether the user has made any edits. Compared by serialised string
    /// equality — the working rule is plain JSON, so this is deterministic
    /// and order-stable (parsed from / written to the same `JSON.parse`
    /// output, so key order is preserved).
    property string _initialSnapshot: ""
    /// True while Kirigami.Dialog's internal ScrollView is reserving space for
    /// a vertical scrollbar (its `rightPadding` is the scrollbar's width, 0
    /// otherwise). Fill-width content keys its right gutter off this so the
    /// gap only appears when there's a scrollbar to clear.
    readonly property bool _scrollBarReserved: contentItem ? contentItem.rightPadding > 0 : false

    signal ruleSaved(var ruleJson)

    /// Open the dialog to edit @p ruleJson. `editing` is no longer a flag —
    /// the only callers of this dialog edit an existing rule (the row's
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

    /// Route every close request (Cancel button, Esc shortcut) through this
    /// gate so the dirty check is honoured uniformly. A clean working rule
    /// closes immediately; a dirty one opens the shared confirm dialog, and
    /// only its "Discard" path closes the dialog.
    ///
    /// Dirty check reads `editorBody.workingRule` (the body's live mutated
    /// copy) rather than `sheet._workingRule` (the openFor seed). The body
    /// owns mutation; the previous shape echoed body changes back into
    /// `_workingRule`, which produced a binding loop under Qt 6.11.
    function _requestClose() {
        if (JSON.stringify(editorBody.workingRule) === sheet._initialSnapshot) {
            sheet.close();
            return;
        }
        discardConfirm.open();
    }

    title: i18nc("@title:window", "Edit Window Rule")
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.9 : Kirigami.Units.gridUnit * 40)
    // Bound the dialog height. Kirigami.Dialog centers itself with
    // `y: (parent.height - height) / 2` (Dialog.qml:300), so `y` depends on
    // `height`; if `height` tracks the content's implicitHeight unbounded, a
    // tall rule's content reflow makes height (and thus y) oscillate — the
    // "Binding loop detected for property y" warning. Capping `maximumHeight`
    // pins `height` so `y` is stable; the content scrolls inside the Dialog's
    // own ScrollView. This is the same pattern WhatsNewPage / ShaderBrowser
    // DetailDialog use (`maximumHeight`, never `preferredHeight`).
    maximumHeight: parent ? Math.min(Kirigami.Units.gridUnit * 40, parent.height * 0.9) : Kirigami.Units.gridUnit * 40
    padding: Kirigami.Units.largeSpacing
    // Hide the header X — it calls close() directly, bypassing the dirty gate.
    // Cancel button + Esc shortcut (both routed through _requestClose) are the
    // only close paths, so unsaved edits always prompt before being discarded.
    showCloseButton: false
    // No standard buttons — Cancel/Save are custom footer actions so Save can
    // gate on `editorBody.canSave` and Cancel can route through the dirty gate.
    standardButtons: Kirigami.Dialog.NoButton
    // `Kirigami.Dialog` defaults to `CloseOnEscape | CloseOnReleaseOutside`; a
    // stray Esc or outside click would discard edits without a prompt. Disable
    // the auto-close paths so every close routes through `_requestClose()`.
    closePolicy: Popup.NoAutoClose

    customFooterActions: [
        Kirigami.Action {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
            onTriggered: sheet._requestClose()
        },
        Kirigami.Action {
            text: i18n("Save")
            icon.name: "dialog-ok-apply"
            enabled: editorBody.canSave
            onTriggered: {
                // Read body's live working rule (not the seed) — see the
                // binding-loop comment on editorBody below.
                sheet.ruleSaved(editorBody.workingRule);
                sheet.close();
            }
        }
    ]

    Shortcut {
        // `sequences` (plural) binds to all key sequences associated with
        // StandardKey.Cancel — Qt warns about only catching one of them when
        // the singular form is used.
        sequences: [StandardKey.Cancel]
        enabled: sheet.opened
        onActivated: sheet._requestClose()
    }

    DiscardChangesDialog {
        id: discardConfirm

        onDiscardConfirmed: sheet.close()
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RuleEditorBody {
            id: editorBody

            Layout.fillWidth: true
            // When the dialog scrolls, its ScrollView reserves the scrollbar's
            // width and fill-width content ends flush against it. Inset the
            // right edge to mirror the left padding — only while a scrollbar
            // is present, else a short dialog gets a lopsided gutter.
            Layout.rightMargin: sheet._scrollBarReserved ? Kirigami.Units.largeSpacing : 0
            controller: sheet.controller
            appSettings: sheet.appSettings
            // Bind workingRule to the dialog's seed — the body emits
            // `workingRuleEdited(next)` for every mutation, and the handler
            // below pushes `next` back into sheet._workingRule. The binding
            // then propagates the new value into editorBody. This keeps the
            // host as the single source of truth and preserves the binding
            // across openFor re-seeds (the prior shape had the body assign
            // `workingRule = next` directly, which broke the binding on first
            // edit and stuck subsequent openFor calls on the previously-edited
            // rule).
            workingRule: sheet._workingRule
            // Guard against a ref-equality echo loop: the body's `_patch`
            // shallow-clones `workingRule`, so every emit hands us a NEW object
            // even when the resulting JSON is identical to the current staged
            // state (e.g. a TextField onEditingFinished that re-fires with the
            // same string). Without this guard, assigning the clone bumps the
            // binding generation, the body re-reads `workingRule`, and
            // validation re-runs through the tree — all on the same logical
            // value.
            onWorkingRuleEdited: next => {
                if (JSON.stringify(sheet._workingRule) !== JSON.stringify(next))
                    sheet._workingRule = next;
            }
        }

        RuleEditorStatusBar {
            Layout.fillWidth: true
            Layout.rightMargin: sheet._scrollBarReserved ? Kirigami.Units.largeSpacing : 0
            canSave: editorBody.canSave
            validationIssues: editorBody.validationIssues
            workingRule: editorBody.workingRule
        }
    }
}
