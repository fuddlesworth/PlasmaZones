// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.settings

/**
 * @brief One rule row in the flat RulesPage priority list.
 *
 * Layout: enabled toggle · name + match summary · `→` · action summary · badges
 * · edit / duplicate / delete. Composite rules show condition / action-count
 * badges. The enabled dot is a toggle; edit / duplicate / delete are buttons.
 */
ExpandableRowDelegate {
    // Drag-reorder lives in the RuleSectionList container (which owns the grip
    // column and the drop cascade), NOT in this delegate: a ColumnLayout-based
    // Repeater snaps items back to their layout position the instant a MouseArea
    // sets `drag.target`, so drag mechanics on the row itself would silently
    // fail. Keeping drag out of this delegate also lets the row stay usable in a
    // non-reorderable host without an "if draggable" branch.

    id: row

    /// Per-rule fields from RuleModel's roles.
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
    /// The rule's raw priority integer — surfaced as a `Priority N` badge on
    /// every row. In the flat list a rule's position already conveys precedence,
    /// but the number stays important when the filter hides categories: the
    /// visible rows are then a subset, so the badge is what tells the user the
    /// true precedence of (and gaps between) the rules still on screen. Matches
    /// the value the Advanced editor exposes and edits.
    required property int priority
    /// True for app-managed System rules (the seeded baseline defaults). They
    /// are non-deletable and pinned, so the delete affordance is shown but
    /// disabled (kept visible to preserve column alignment).
    property bool managed: false
    /// Localized section name shown as a small category badge, so category stays
    /// legible in the flat (ungrouped) list. Empty hides the badge.
    property string sectionLabel: ""
    /// RuleController — needed to resolve a rule's match JSON
    /// on-demand for the expansion view (`controller.ruleJson(ruleId).match`).
    /// `null` disables expansion entirely; expansion-capable callers must
    /// thread the controller through.
    property var controller: null
    /// Cached `controller.matchFields()` — used by MatchExpressionView to
    /// translate wire strings back to user-facing labels. Threaded down by
    /// RulesPage so each row doesn't re-invoke the Q_INVOKABLE.
    property var matchFieldOptions: []
    /// Cached `controller.actionTypes()` — same threading pattern as
    /// matchFieldOptions. Used by ActionListView to resolve action-type
    /// wire strings and per-param labels in the read-only THEN section.
    property var actionTypeOptions: []
    /// Composite app-settings surface (layouts + animationsController)
    /// passed through to ActionListView so the read-only THEN section
    /// can resolve picker-aware param values (layout UUIDs, animation
    /// events, shader effects, curves) to the same labels the rule
    /// editor shows.
    property var appSettings: null
    /// The expandable-row shell (hover highlight, whole-row click-to-expand,
    /// animated clip, lazy body Loader) is inherited from
    /// ExpandableRowDelegate; the body is the read-only WHEN/THEN preview,
    /// disabled when no controller is threaded in.
    expansionContent: row.controller !== null ? expansionComponent : null

    signal editRequested
    signal duplicateRequested
    signal deleteRequested
    // Parameter named `ruleEnabled` for symmetry with the `ruleEnabled`
    // property — using the bare name `enabled` would shadow the row's own
    // `enabled` in any handler that relies on implicit-argument scope.
    signal toggleRequested(bool ruleEnabled)

    // Section-header pill shared by the WHEN and THEN halves of the expanded
    // preview. Both headers use one capsule style — highlightColor family,
    // 0.4 fill + 0.9 border — so the two sections read as one design and line
    // up on the same left edge. Mirrors the fill/border recipe of the
    // ALL/ANY/NONE group pills in MatchExpressionView.
    component SectionHeaderPill: Rectangle {
        property alias text: pillLabel.text

        implicitWidth: pillLabel.implicitWidth + Kirigami.Units.largeSpacing * 2
        implicitHeight: pillLabel.implicitHeight + Kirigami.Units.smallSpacing * 2
        radius: implicitHeight / 2
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4)
        border.width: Math.max(1, Math.round(Screen.devicePixelRatio))
        border.color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.9)

        Label {
            id: pillLabel

            anchors.centerIn: parent
            font.bold: true
            font.capitalization: Font.AllUppercase
            font: Kirigami.Theme.smallFont
            color: Kirigami.Theme.textColor
        }
    }

    // Instantiated inside a Repeater/ColumnLayout — `Layout.fillWidth: true`
    // (set by the delegate's parent) drives the width. The hover highlight,
    // the whole-row click-to-expand handling (toolbar buttons and the
    // SettingsSwitch consume their own clicks first), and the animated
    // expansion clip all come from the shared ExpandableRowDelegate shell;
    // the children below are its header-row content.

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
        onToggled: function (newValue) {
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
        ToolTip.delay: Kirigami.Units.toolTipDelay
        ToolTip.text: i18np("This rule has %n validation issue. Open the editor to see the details.", "This rule has %n validation issues. Open the editor to see the details.", row.validationIssueCount)

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

    // Section badge — the rule's category (Monitor / Application / …),
    // shown first in the cluster so the flat priority list stays legible
    // by category without grouping. Highlight-tinted (theme-derived, not
    // hardcoded) to read as a category marker distinct from the neutral
    // metadata badges that follow.
    Rectangle {
        visible: row.sectionLabel.length > 0
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: sectionBadgeLabel.implicitWidth + Kirigami.Units.largeSpacing
        implicitHeight: sectionBadgeLabel.implicitHeight + Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.18)

        Label {
            id: sectionBadgeLabel

            anchors.centerIn: parent
            text: row.sectionLabel
            font: Kirigami.Theme.smallFont
            opacity: 0.85
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
            text: i18nc("Badge shown when the rule's match is a composite expression", "Composite")
            font: Kirigami.Theme.smallFont
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
            font: Kirigami.Theme.smallFont
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
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }

    // Priority badge — kept on every row so precedence stays legible even
    // when the filter hides categories (the visible rows are then a subset,
    // so position alone can't convey the true ordering). Managed System
    // rules are pinned to INT_MIN, so show that intent, not the raw number.
    Rectangle {
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: priorityLabel.implicitWidth + Kirigami.Units.largeSpacing
        implicitHeight: priorityLabel.implicitHeight + Kirigami.Units.smallSpacing
        radius: Kirigami.Units.smallSpacing
        color: Kirigami.Theme.alternateBackgroundColor

        Label {
            id: priorityLabel

            anchors.centerIn: parent
            text: row.managed ? i18nc("Priority badge for a managed baseline rule that always sorts last", "Lowest priority") : i18nc("Badge showing the rule's raw priority integer", "Priority %1", row.priority)
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }

    // Doubles as the expand-state indicator AND the visual separator
    // between the match and action halves (shared ExpandChevron).
    ExpandChevron {
        expanded: row.expanded
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
        // Managed System rules (the seeded defaults) are non-deletable —
        // shown but disabled, so the button keeps its column alignment
        // with the deletable rows in other sections.
        enabled: !row.managed
        icon.name: "edit-delete"
        Layout.alignment: Qt.AlignVCenter
        ToolTip.text: row.managed ? i18n("System rules can't be deleted") : i18n("Delete rule")
        ToolTip.visible: hovered
        Accessible.name: i18n("Delete rule %1", row.ruleName)
        onClicked: row.deleteRequested()
    }

    // Lazily-loaded read-only preview of the rule's full match tree,
    // hosted by the shell's expansion Loader (see expansionContent above).
    // The controller's `ruleJson` returns a deep copy of the rule, so the
    // View walks its own data and a controller-side mutation won't yank
    // the tree mid-render.
    Component {
        id: expansionComponent

        ColumnLayout {
            readonly property var _ruleJson: row.controller ? row.controller.ruleJson(row.ruleId) : ({})

            spacing: Kirigami.Units.smallSpacing

            SectionHeaderPill {
                Layout.alignment: Qt.AlignLeft
                text: i18n("WHEN")
            }

            MatchExpressionView {
                Layout.fillWidth: true
                matchJson: parent._ruleJson.match || ({
                        "all": []
                    })
                controller: row.controller
                matchFieldOptions: row.matchFieldOptions
                appSettings: row.appSettings
            }

            SectionHeaderPill {
                Layout.alignment: Qt.AlignLeft
                Layout.topMargin: Kirigami.Units.smallSpacing
                text: i18n("THEN")
            }

            ActionListView {
                Layout.fillWidth: true
                actionsJson: parent._ruleJson.actions || []
                actionTypeOptions: row.actionTypeOptions
                appSettings: row.appSettings
            }
        }
    }
}
