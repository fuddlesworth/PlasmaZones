// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief One rule row in the grouped WindowRulesPage list.
 *
 * Layout mirrors the SVG mockup: enabled dot · match summary · `→` · action
 * summary · edit / delete. Composite rules show condition / action-count
 * badges. The enabled dot is a toggle; edit / delete are buttons.
 */
ItemDelegate {
    // Drag-reorder for the Animation section lives in WindowRulesPage's
    // dedicated drag container (mirrors OrderingPage's pattern), NOT here:
    // a ColumnLayout-based Repeater snaps items back to their layout
    // position the instant a MouseArea sets `drag.target`, so any drag
    // mechanics on the row itself silently fail. Keeping drag concerns out
    // of this delegate also means the row stays usable in non-reorderable
    // contexts (other sections) without an "if draggable" branch.

    id: row

    /// Per-rule fields from WindowRuleModel's roles.
    required property string ruleId
    required property string ruleName
    /// The rule's own enabled flag — distinct from `ItemDelegate.enabled`,
    /// which gates the delegate's interactivity.
    required property bool ruleEnabled
    required property string matchSummary
    required property string actionSummary
    required property int conditionCount
    required property int actionCount
    required property bool isComposite
    /// Number of semantic-validation issues the rule carries. A non-zero
    /// count surfaces a warning icon next to the rule name so the user
    /// knows the rule never fires (the picker now prevents the combination
    /// for new rules, but a hand-edited JSON store keeps the offending rule).
    required property int validationIssueCount
    /// The rule's section bucket (`WindowRuleModel::Section` int). Drives the
    /// Advanced-only `priority N` label — only Advanced rules surface their
    /// raw priority in the row because their priority is the user-controlled
    /// ordering dimension (other sections derive priority from cascade bands).
    required property int section
    /// The rule's raw priority integer — surfaced as a `Priority N` badge in
    /// the badge cluster on Advanced-section rows only.
    required property int priority

    signal editRequested()
    signal duplicateRequested()
    signal deleteRequested()
    // Parameter named `ruleEnabled` for symmetry with the `ruleEnabled`
    // property — using the bare name `enabled` would shadow the row's own
    // `enabled` in any handler that relies on implicit-argument scope.
    signal toggleRequested(bool ruleEnabled)

    // Instantiated inside a Repeater/ColumnLayout — `Layout.fillWidth: true`
    // (set by the delegate's parent) drives the width; there is no enclosing
    // ListView, so no `ListView.view` branch.
    hoverEnabled: true

    contentItem: RowLayout {
        spacing: Kirigami.Units.largeSpacing

        // Enable/disable toggle. Replaces an earlier filled-dot affordance
        // that read as decorative: users couldn't tell it was clickable, and
        // the disabled-state hollow ring wasn't obviously distinct. The
        // SettingsSwitch is the same control the rest of the settings app
        // uses for boolean toggles (animated knob via PhosphorMotionAnimation,
        // PointingHandCursor on hover), so the affordance matches the user's
        // existing mental model and screen-reader output stays unchanged.
        SettingsSwitch {
            Layout.alignment: Qt.AlignVCenter
            checked: row.ruleEnabled
            accessibleName: row.ruleEnabled ? i18n("Disable rule %1", row.ruleName) : i18n("Enable rule %1", row.ruleName)
            onToggled: function(newValue) {
                row.toggleRequested(newValue);
            }
        }

        // Warning badge — sits before the rule name so it's the first thing
        // the eye hits when scanning the list. Hovering reveals the count
        // explanation; the editor sheet itself shows the full per-issue
        // messages. Hidden when the rule is well-formed.
        Kirigami.Icon {
            visible: row.validationIssueCount > 0
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            source: "dialog-warning"
            Accessible.name: i18np("%n validation issue", "%n validation issues", row.validationIssueCount)
            ToolTip.visible: warningHover.hovered
            ToolTip.delay: 300
            ToolTip.text: i18n("This rule has %1 validation issue(s) — open the editor to see the details. The rule will not fire as written.", row.validationIssueCount)

            HoverHandler {
                id: warningHover
            }

        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: row.ruleName.length > 0 ? row.ruleName : row.matchSummary
                font.bold: true
                opacity: row.ruleEnabled ? 1 : 0.5
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: row.matchSummary
                opacity: row.ruleEnabled ? 0.7 : 0.4
                elide: Text.ElideRight
                visible: row.ruleName.length > 0
            }

        }

        // "Composite" badge — paired with the conditions count for composite
        // rules. Surfaces the kind of rule the user is looking at without
        // making them open the editor. Sentence case (project convention; the
        // mockup's uppercase is intentionally not followed).
        Rectangle {
            visible: row.isComposite
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: compositeLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: compositeLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: compositeLabel

                anchors.centerIn: parent
                // Project badge convention: title-case the noun ("Composite",
                // "Conditions", "Actions", "Priority …"). Same in the
                // condition/action/priority badges below — keeping them
                // consistent so the eye reads the badge cluster as a unit.
                text: i18nc("Badge — the rule's match is a composite expression", "Composite")
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }

        }

        // Condition-count badge for composite rules.
        Rectangle {
            visible: row.isComposite
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: condLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: condLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: condLabel

                anchors.centerIn: parent
                text: i18np("%n Condition", "%n Conditions", row.conditionCount)
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }

        }

        // Action-count badge — shown for rules carrying more than one action.
        Rectangle {
            visible: row.actionCount > 1
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: actionLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: actionLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: actionLabel

                anchors.centerIn: parent
                text: i18np("%n Action", "%n Actions", row.actionCount)
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }

        }

        // Priority badge — sits with the other metadata badges (Composite,
        // Conditions, Actions). Shown only for Advanced-section rules where
        // the raw priority integer is the user-controlled ordering dimension.
        // Other sections derive priority from cascade bands so the raw number
        // adds noise rather than information. Section enum value 4 =
        // WindowRuleModel::Section::Advanced.
        Rectangle {
            visible: row.section === 4
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: priorityLabel.implicitWidth + Kirigami.Units.largeSpacing
            implicitHeight: priorityLabel.implicitHeight + Kirigami.Units.smallSpacing
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.alternateBackgroundColor

            Label {
                id: priorityLabel

                anchors.centerIn: parent
                text: i18nc("Badge — the rule's raw priority integer", "Priority %1", row.priority)
                font.pointSize: Kirigami.Theme.smallFont.pointSize
                opacity: 0.7
            }

        }

        Kirigami.Icon {
            source: "arrow-right"
            Layout.preferredWidth: Kirigami.Units.iconSizes.small
            Layout.preferredHeight: Kirigami.Units.iconSizes.small
            Layout.alignment: Qt.AlignVCenter
            opacity: 0.6
        }

        // Action summary — wraps to two lines before eliding so multi-action
        // labels like "Engine: Snapping · Snapping: Portrait 2-Row" don't get
        // cut at "Portrait..." (a constant-width single-line elide makes the
        // user open the editor just to see which layout the rule picks).
        Label {
            Layout.alignment: Qt.AlignVCenter
            Layout.maximumWidth: Kirigami.Units.gridUnit * 22
            Layout.preferredWidth: Kirigami.Units.gridUnit * 18
            elide: Text.ElideRight
            font.bold: true
            maximumLineCount: 2
            opacity: row.ruleEnabled ? 1 : 0.5
            text: row.actionSummary
            wrapMode: Text.WordWrap
        }

        ToolButton {
            icon.name: "document-edit"
            Layout.alignment: Qt.AlignVCenter
            ToolTip.text: i18n("Edit rule")
            ToolTip.visible: hovered
            Accessible.name: i18n("Edit rule %1", row.ruleName)
            onClicked: row.editRequested()
        }

        // Duplicate — clones the rule with a fresh UUID and inserts it just
        // after the original. Cheap way to template a near-identical rule
        // (same match, tweak the actions; or same actions, tweak the match)
        // without re-entering the editor from scratch.
        ToolButton {
            icon.name: "edit-copy"
            Layout.alignment: Qt.AlignVCenter
            ToolTip.text: i18n("Duplicate rule")
            ToolTip.visible: hovered
            Accessible.name: i18n("Duplicate rule %1", row.ruleName)
            onClicked: row.duplicateRequested()
        }

        ToolButton {
            icon.name: "edit-delete"
            Layout.alignment: Qt.AlignVCenter
            ToolTip.text: i18n("Delete rule")
            ToolTip.visible: hovered
            Accessible.name: i18n("Delete rule %1", row.ruleName)
            onClicked: row.deleteRequested()
        }

    }

}
