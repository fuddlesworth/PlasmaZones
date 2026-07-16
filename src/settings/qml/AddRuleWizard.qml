// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Multi-step wizard for creating a new rule.
 *
 * Step 1: Choose a starting point — Quick-start templates + bare subjects
 *         (delegated to `RuleStartPicker`).
 * Step 2: Configure — name / enabled / priority / WHEN / THEN
 *         (delegated to `RuleEditorBody`, the same body the edit
 *         `RuleEditorSheet` mounts so the authoring UI exists once).
 *
 * Same `Kirigami.Dialog` + `WizardStepIndicator` + `WizardFooter` framework
 * as `NewLayoutDialog`/`NewAlgorithmDialog` — wizard primitives live in
 * `Wizard*.qml` and are reused verbatim.
 *
 * Close gating mirrors `RuleEditorSheet`: a snapshot of the working rule is
 * taken when it's first seeded (picker step → editor step), and Cancel /
 * Esc / outside-click route through `_requestClose()`. The Back button
 * during step 2 also routes through `_requestBack()` since stepping back to
 * the picker throws away the staged rule — same destructive UX as cancel,
 * same prompt.
 */
Kirigami.Dialog {
    id: root

    /// The RuleController — threaded into the picker (templates /
    /// subjects) and the editor body (action types / match fields / Save
    /// gate / validation).
    required property var controller
    /// The SettingsController bridge — threaded into the editor body so the
    /// picker-kind params (screen / activity / layout) populate from real
    /// catalogues instead of showing raw ids.
    required property var appSettings
    property int currentStep: 0
    /// The freshly-stamped rule the picker handed off — set on `chosen` in
    /// step 1, mutated in place by the body in step 2. Cancel / re-pick
    /// throws away this reference; the parent only sees a final rule when
    /// `ruleSaved` fires.
    property var _workingRule: ({})
    /// JSON snapshot of the working rule at the moment we entered step 2
    /// (i.e. immediately after the picker handed us the seeded rule). Used
    /// by `_requestClose` / `_requestBack` to decide whether to surface the
    /// discard prompt. Empty string ⇒ no rule has been picked yet
    /// (currentStep == 0); in that case there is nothing to lose, so the
    /// close paths bypass the prompt.
    property string _initialSnapshot: ""
    /// True while Kirigami.Dialog's internal ScrollView is reserving space for
    /// a vertical scrollbar (its `rightPadding` is the scrollbar's width, 0
    /// otherwise). Fill-width content keys its right gutter off this so the
    /// gap only appears when there's a scrollbar to clear — without it, a
    /// short dialog (no scrollbar) would show a lopsided right margin.
    readonly property bool _scrollBarReserved: contentItem ? contentItem.rightPadding > 0 : false

    /// Emitted with the final rule JSON when the user clicks Create.
    /// `RulesPage` wires this into `controller.addRuleFromJson`.
    signal ruleSaved(var ruleJson)

    function _onChosen(kind, id, ruleJson) {
        // Picker emits → stage the rule and advance. Stash the chosen
        // template/subject id so a debug log can trace which starting
        // point produced an issue, but we don't gate behaviour on it.
        root._workingRule = ruleJson;
        // Snapshot the picker payload directly (vs. reading
        // `editorBody.workingRule` via Qt.callLater) — the previous
        // shape deferred the snapshot to the next event loop turn so
        // editorBody's binding could propagate, but the deferred read
        // left a brief timing window during which `_isClean()` saw
        // `_initialSnapshot == ""` while a real working rule was
        // already staged. An Esc keystroke (or an outside click in
        // the brief window between currentStep flip and the deferred
        // snapshot) would bypass the dirty-discard prompt because the
        // clean check returns true on the still-empty snapshot.
        //
        // Snapshotting `ruleJson` directly avoids the binding-
        // propagation race: the picker's payload IS the staged rule,
        // and `editorBody.workingRule` will converge to the same JSON
        // once the binding settles.
        root._initialSnapshot = JSON.stringify(ruleJson);
        // Clear any "Pick a starting point first." error left over from a
        // Next click with nothing picked — it would otherwise linger in the
        // step-2 footer.
        wizardFooter.errorText = "";
        root.currentStep = 1;
    }

    /// Close gate: clean working rule (step 1 with nothing picked, or
    /// step 2 with no body edits) closes immediately; otherwise the shared
    /// confirm dialog gates the destructive close.
    function _requestClose() {
        if (root._isClean()) {
            root.close();
            return;
        }
        discardConfirm.open();
    }

    /// Back gate: stepping from the editor back to the picker drops the
    /// staged rule (a re-pick rebuilds the working state from scratch), so
    /// any body edits would be silently lost. Same prompt as cancel — only
    /// "Discard" actually steps back.
    function _requestBack() {
        if (root._isClean()) {
            root._goBack();
            return;
        }
        discardBackConfirm.open();
    }

    function _goBack() {
        root.currentStep = 0;
        root._workingRule = ({});
        root._initialSnapshot = "";
    }

    /// Reset the dialog's scroll offset to the top of the current page.
    ///
    /// `Kirigami.Dialog` wraps its content in an internal `QQC2.ScrollView`
    /// (exposed as `contentItem` — see `_scrollBarReserved`), whose own
    /// `contentItem` is the backing `Flickable`. That Flickable keeps its
    /// `contentY` across a `StackLayout` page switch, so the offset the step-1
    /// picker had carries into step 2 and scrolls the editor's WHEN section off
    /// the top. Snapping `contentY` back to 0 on each transition makes every
    /// step open at the top of its page.
    function _scrollToTop() {
        const scrollView = root.contentItem;
        const flickable = scrollView ? scrollView.contentItem : null;
        if (flickable && flickable.contentY !== undefined)
            flickable.contentY = 0;
    }

    function _isClean() {
        // Step 1 has no working rule to lose. Step 2 is dirty iff the
        // current working rule diverges from the snapshot we took on entry.
        //
        // Read body's live workingRule (not the picker-seeded
        // root._workingRule). The previous shape echoed body edits back
        // into _workingRule, which produced a Qt 6.11 binding loop.
        if (root.currentStep === 0)
            return true;

        return JSON.stringify(editorBody.workingRule) === root._initialSnapshot;
    }

    title: i18nc("@title:window", "New Rule")
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.9 : Kirigami.Units.gridUnit * 40)
    standardButtons: Kirigami.Dialog.NoButton
    padding: Kirigami.Units.largeSpacing
    // `Kirigami.Dialog` defaults to `CloseOnEscape | CloseOnReleaseOutside`;
    // a stray Esc or click outside the dialog would wipe the staged rule
    // without a prompt. Disable the auto-close paths so every close routes
    // through `_requestClose()` (Cancel button, our own Shortcut for Esc).
    // The title-bar X button calls `reject()` directly and bypasses
    // `closePolicy`, so hide it entirely — same pattern as RuleEditorSheet.
    closePolicy: Popup.NoAutoClose
    showCloseButton: false
    onOpened: {
        // Reset state on every open so a previously-cancelled wizard doesn't
        // carry stale picker selection / draft rule into the next session.
        root.currentStep = 0;
        root._workingRule = ({});
        root._initialSnapshot = "";
        wizardFooter.errorText = "";
    }
    // Every step transition (picker → editor, Next fallback, Back → picker)
    // should reveal the top of the new page. Deferred via `Qt.callLater` so the
    // StackLayout has switched pages and the Dialog's content-height binding has
    // settled before we snap the Flickable back to 0 — setting it earlier would
    // be clobbered by the post-switch relayout.
    onCurrentStepChanged: Qt.callLater(root._scrollToTop)

    Shortcut {
        // `sequences` (plural) binds all key sequences associated with
        // StandardKey.Cancel — the singular form only catches one.
        sequences: [StandardKey.Cancel]
        enabled: root.opened
        onActivated: root._requestClose()
    }

    DiscardChangesDialog {
        id: discardConfirm

        onDiscardConfirmed: root.close()
    }

    DiscardChangesDialog {
        id: discardBackConfirm

        // The Back path is functionally a discard-and-restart-from-picker —
        // same destructive UX as cancel, so the same prompt copy is fine.
        // The only difference is the post-confirm action: don't close the
        // wizard, just rewind it to step 1.
        onDiscardConfirmed: root._goBack()
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        WizardStepIndicator {
            stepLabels: [i18n("Starting point"), i18n("Configure")]
            currentStep: root.currentStep
        }

        Kirigami.Separator {
            Layout.fillWidth: true
            // When the dialog scrolls, its ScrollView reserves the scrollbar's
            // width and fill-width content ends flush against it. Inset the
            // right edge to mirror the left padding — but only while a
            // scrollbar is present, else a short dialog gets a lopsided gutter.
            Layout.rightMargin: root._scrollBarReserved ? Kirigami.Units.largeSpacing : 0
        }

        StackLayout {
            id: pageStack

            Layout.fillWidth: true
            // See the Separator above: clear the dialog scrollbar (when shown)
            // so the cards / step-2 editor keep a gutter matching the left.
            Layout.rightMargin: root._scrollBarReserved ? Kirigami.Units.largeSpacing : 0
            currentIndex: root.currentStep

            // ── Step 1: starting point ─────────────────────────────────
            RuleStartPicker {
                Layout.fillWidth: true
                controller: root.controller
                onChosen: function (kind, id, ruleJson) {
                    root._onChosen(kind, id, ruleJson);
                }
            }

            // ── Step 2: configure ──────────────────────────────────────
            // Body + StatusBar mirror RuleEditorSheet's structure: same
            // body widget owns the working-rule mutations, same status bar
            // surfaces completeness / validation, footer Save button gates
            // on `editorBody.canSave`.
            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing

                RuleEditorBody {
                    id: editorBody

                    Layout.fillWidth: true
                    controller: root.controller
                    appSettings: root.appSettings
                    // Bind workingRule to the wizard seed; the body
                    // emits workingRuleEdited(next) and we push back
                    // into root._workingRule. The binding then
                    // propagates the new value back into editorBody,
                    // keeping the wizard as the single source of
                    // truth and preserving the binding across
                    // _goBack / re-pick cycles (the prior shape had
                    // the body assign workingRule directly, breaking
                    // the binding on first edit).
                    workingRule: root._workingRule
                    // Guard against a ref-equality echo loop: the body's
                    // `_patch` shallow-clones `workingRule`, so every emit
                    // hands us a NEW object even when the resulting JSON
                    // is identical (e.g. a TextField onEditingFinished
                    // that re-fires with the same string). Without this
                    // guard, the assignment bumps the binding generation
                    // and the body re-reads + re-validates on every
                    // keystroke that didn't actually change content.
                    onWorkingRuleEdited: next => {
                        if (JSON.stringify(root._workingRule) !== JSON.stringify(next))
                            root._workingRule = next;
                    }
                }

                RuleEditorStatusBar {
                    Layout.fillWidth: true
                    canSave: editorBody.canSave
                    validationIssues: editorBody.validationIssues
                    workingRule: editorBody.workingRule
                }
            }
        }
    }

    footer: WizardFooter {
        id: wizardFooter

        currentStep: root.currentStep
        createText: i18n("Add rule")
        // Step 2's body gates Save on a complete + semantically-clean rule
        // exactly like the edit sheet; the wizard footer reuses that.
        createEnabled: editorBody.canSave
        onBackClicked: root._requestBack()
        onNextClicked: {
            // Picker auto-advances on tile click; this button is a fallback
            // (e.g. keyboard nav) and should advance only if a starting
            // point has been chosen. An empty working rule means the user
            // hit Next without picking.
            if (root._workingRule && root._workingRule.id)
                root.currentStep = 1;
            else
                wizardFooter.errorText = i18n("Pick a starting point first.");
        }
        onCreateClicked: {
            wizardFooter.errorText = "";
            // Read body's live workingRule (not the picker seed) — see
            // the binding-loop comment on editorBody above.
            root.ruleSaved(editorBody.workingRule);
            root.close();
        }
        onCancelClicked: root._requestClose()
    }
}
