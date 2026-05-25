// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Multi-step wizard for creating a new window rule.
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
 */
Kirigami.Dialog {
    id: root

    /// The WindowRuleController — threaded into the picker (templates /
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
    property var _workingRule: ({
    })

    /// Emitted with the final rule JSON when the user clicks Create.
    /// `WindowRulesPage` wires this into `controller.addRuleFromJson`.
    signal ruleSaved(var ruleJson)

    function _onChosen(kind, id, ruleJson) {
        // Picker emits → stage the rule and advance. Stash the chosen
        // template/subject id so a debug log can trace which starting
        // point produced an issue, but we don't gate behaviour on it.
        root._workingRule = ruleJson;
        root.currentStep = 1;
    }

    title: i18nc("@title:window", "New Window Rule")
    preferredWidth: Math.min(Kirigami.Units.gridUnit * 40, parent ? parent.width * 0.9 : Kirigami.Units.gridUnit * 40)
    standardButtons: Kirigami.Dialog.NoButton
    padding: Kirigami.Units.largeSpacing
    onOpened: {
        // Reset state on every open so a previously-cancelled wizard doesn't
        // carry stale picker selection / draft rule into the next session.
        root.currentStep = 0;
        root._workingRule = ({
        });
        wizardFooter.errorText = "";
    }

    ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        WizardStepIndicator {
            stepLabels: [i18n("Starting point"), i18n("Configure")]
            currentStep: root.currentStep
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        StackLayout {
            id: pageStack

            Layout.fillWidth: true
            currentIndex: root.currentStep

            // ── Step 1: starting point ─────────────────────────────────
            RuleStartPicker {
                Layout.fillWidth: true
                controller: root.controller
                onChosen: function(kind, id, ruleJson) {
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
                    workingRule: root._workingRule
                    onWorkingRuleChanged: {
                        // Body owns mutation — propagate its edits back into
                        // the wizard's working-rule mirror so Create reads
                        // the latest state. Without this echo, Create would
                        // submit the picker's seed rule un-edited.
                        root._workingRule = workingRule;
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
        onBackClicked: {
            // Back to picker clears the working rule so a re-pick can't
            // accidentally merge into half-edited state. Same UX as
            // restarting from the Add-rule button.
            root.currentStep = 0;
            root._workingRule = ({
            });
        }
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
            root.ruleSaved(root._workingRule);
            root.close();
        }
        onCancelClicked: root.close()
    }

}
