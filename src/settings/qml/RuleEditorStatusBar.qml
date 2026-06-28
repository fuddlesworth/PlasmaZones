// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The slot-priority helper hint + completeness gate + semantic
 *        validation inline messages.
 *
 * Lives in the footer of both `RuleEditorSheet` and `AddRuleWizard` so the
 * messaging surface stays identical between the create and edit flows.
 * Bound to a `RuleEditorBody` via the consumer's working-rule props.
 */
ColumnLayout {
    id: root

    /// True iff the working rule is currently saveable — drives whether the
    /// Information completeness gate ("Add at least one action…") fires.
    required property bool canSave
    /// Per-rule validation issues from the controller's
    /// `validationIssuesForJson`. Each entry: `{ code, actionIndex,
    /// actionType, message }`. Non-empty triggers the Warning message.
    required property var validationIssues
    /// The working rule itself — read to disambiguate the completeness
    /// message between "no actions" and "blank match leaf".
    required property var workingRule

    spacing: Kirigami.Units.smallSpacing

    // Helper hint. Pinned in the consumer's footer so a tall rule (many
    // conditions / actions) can't push it out of view on a small window.
    // `Layout.preferredWidth: 0` is the canonical anti-binding-loop guard
    // for wrap-text labels inside OverlaySheet/Dialog implicit-height
    // chains — without it the host complains about implicitHeight loops.
    Label {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        wrapMode: Text.WordWrap
        font.italic: true
        opacity: 0.6
        text: i18n("The first rule (by priority) to fill each action slot wins that slot. Actions in different slots stack.")
    }

    // Completeness gate — shown when Save is blocked for a structural
    // reason (missing action / blank leaf) that is NOT a semantic
    // validation issue.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        type: Kirigami.MessageType.Information
        visible: !root.canSave && root.validationIssues.length === 0
        text: !root.workingRule.actions || root.workingRule.actions.length === 0 ? i18n("Add at least one action before saving.") : i18n("Every condition needs a value before this rule can be saved.")
    }

    // Semantic-validation surface — lifts the rule library's diagnostics into the
    // editor. Each line is localized here from the issue `code` (the lib's
    // `message` is English-only, for logging — see PhosphorRules::Rule.h). Save is
    // also gated on this list via canSave.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        type: Kirigami.MessageType.Warning
        visible: root.validationIssues.length > 0
        text: {
            if (root.validationIssues.length === 0)
                return "";

            // Issue codes mirror PhosphorRules::ValidationIssue::Code:
            //   0 = ContextActionWithWindowMatch (the action never fires)
            //   1 = TerminalActionWithEffectActions (the action may not apply)
            var lines = [];
            for (var i = 0; i < root.validationIssues.length; ++i) {
                var issue = root.validationIssues[i];
                if (issue.code === 1) {
                    lines.push("• " + i18n("Action “%1” may not take effect because this rule also excludes the window. Put the exclusion on a separate rule.", issue.actionType));
                } else {
                    lines.push("• " + i18n("Action “%1” never fires: it is a context action, but the rule matches window properties.", issue.actionType));
                }
            }
            var heading = root.validationIssues.length === 1 ? i18n("This rule has a problem:") : i18n("This rule has problems:");
            return heading + "\n" + lines.join("\n");
        }
    }
}
