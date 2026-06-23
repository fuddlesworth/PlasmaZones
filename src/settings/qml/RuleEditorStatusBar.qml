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

    // Semantic-validation surface — lifts the daemon's qCWarning into the
    // editor so the user sees exactly why the rule would never fire. Save
    // is also gated on this list via canSave.
    Kirigami.InlineMessage {
        Layout.fillWidth: true
        Layout.preferredWidth: 0
        type: Kirigami.MessageType.Warning
        visible: root.validationIssues.length > 0
        text: {
            if (root.validationIssues.length === 0)
                return "";

            var lines = [];
            for (var i = 0; i < root.validationIssues.length; ++i) {
                lines.push("• " + root.validationIssues[i].message);
            }
            return i18n("This rule would never fire:") + "\n" + lines.join("\n");
        }
    }
}
