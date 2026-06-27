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
 * @brief One rule row in the grouped RulesPage list.
 *
 * Layout mirrors the SVG mockup: enabled dot · match summary · `→` · action
 * summary · edit / delete. Composite rules show condition / action-count
 * badges. The enabled dot is a toggle; edit / delete are buttons.
 */
ItemDelegate {
    // Drag-reorder for the Animation section lives in RulesPage's
    // dedicated drag container (mirrors OrderingPage's pattern), NOT here:
    // a ColumnLayout-based Repeater snaps items back to their layout
    // position the instant a MouseArea sets `drag.target`, so any drag
    // mechanics on the row itself silently fail. Keeping drag concerns out
    // of this delegate also means the row stays usable in non-reorderable
    // contexts (other sections) without an "if draggable" branch.

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
    /// The rule's raw priority integer — surfaced as a `Priority N` badge in
    /// the badge cluster on every row, so each rule's effective ordering
    /// priority is visible regardless of section.
    required property int priority
    /// True for app-managed System rules (the seeded baseline defaults). They
    /// are non-deletable and pinned, so the delete affordance is hidden.
    property bool managed: false
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
    /// True when the row should surface its expand affordance. The Animation
    /// drag container depends on a fixed row height and sets this to false
    /// so an expanded row can't break the drag math; every other section
    /// keeps the default (true).
    property bool expandable: true
    /// Current expansion state — toggled by the chevron on the row. Lives
    /// on the delegate so each row expands independently; resets on page
    /// reload (acceptable — the rule list is the page's primary content
    /// and the expansion is a peek, not a persistent view mode).
    property bool expanded: false

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
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            color: Kirigami.Theme.textColor
        }
    }

    // Instantiated inside a Repeater/ColumnLayout — `Layout.fillWidth: true`
    // (set by the delegate's parent) drives the width; there is no enclosing
    // ListView, so no `ListView.view` branch.
    hoverEnabled: true
    // Whole-row click toggles expansion (Option C). The toolbar buttons
    // (edit / duplicate / delete) and the SettingsSwitch consume their own
    // clicks, so a click on those never reaches this handler — the user can
    // hit Edit / Duplicate / Delete / Toggle without accidentally expanding.
    // Click events on the name/match-summary labels and badge area DO bubble
    // up here, so clicking the body of the row toggles expansion. The
    // `arrow-right` icon's rotation signals the state.
    onClicked: {
        if (row.expandable)
            row.expanded = !row.expanded;
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
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
                ToolTip.delay: 300
                ToolTip.text: i18np("This rule has %n validation issue. Open the editor to see the details. The rule will not fire as written.", "This rule has %n validation issues. Open the editor to see the details. The rule will not fire as written.", row.validationIssueCount)

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
                    text: i18nc("Badge shown when the rule's match is a composite expression", "Composite")
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
            // Conditions, Actions). Shown on every row so each rule's effective
            // ordering priority is visible across all sections, not just
            // Advanced (other sections derive their priority from cascade bands,
            // but surfacing the resolved number is still informative).
            Rectangle {
                Layout.alignment: Qt.AlignVCenter
                implicitWidth: priorityLabel.implicitWidth + Kirigami.Units.largeSpacing
                implicitHeight: priorityLabel.implicitHeight + Kirigami.Units.smallSpacing
                radius: Kirigami.Units.smallSpacing
                color: Kirigami.Theme.alternateBackgroundColor

                Label {
                    id: priorityLabel

                    anchors.centerIn: parent
                    // Managed System rules are pinned to INT_MIN so they always
                    // sort below user rules — show that intent, not the raw number.
                    text: row.managed ? i18nc("Priority badge for a managed baseline rule that always sorts last", "Lowest priority") : i18nc("Badge showing the rule's raw priority integer", "Priority %1", row.priority)
                    font.pointSize: Kirigami.Theme.smallFont.pointSize
                    opacity: 0.7
                }
            }

            // Doubles as the expand-state indicator: rotates 90° clockwise when
            // `row.expanded` so the same icon serves as the visual separator
            // between match and action AND as a chevron pointing at the
            // expanded body. The whole-row `onClicked` handler above owns the
            // toggle; this is a passive indicator (no MouseArea), which keeps
            // it from competing with the row click for the same press event.
            Kirigami.Icon {
                source: "arrow-right"
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
                Layout.alignment: Qt.AlignVCenter
                opacity: 0.6
                rotation: row.expanded ? 90 : 0

                Behavior on rotation {
                    PhosphorMotionAnimation {
                        profile: "widget.hover"
                        durationOverride: Kirigami.Units.shortDuration
                    }
                }
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
        }
        // ── Expansion area ───────────────────────────────────────────────

        // Clipped container that animates the expansion in/out instead of
        // snapping height between 0 and the full body. Mirrors SettingsCard's
        // expand/collapse pattern: clip:true keeps the loaded body from
        // bleeding above the row during the height interpolation, and
        // PhosphorMotionAnimation on `Layout.preferredHeight` + `opacity`
        // hands the timing off to the project's motion profile (so
        // animation-speed and reduce-motion preferences propagate without
        // a hardcoded duration). The Loader stays `active` for one extra
        // animation cycle when collapsing so the body can fade out
        // gracefully instead of being torn down mid-transition.
        Item {
            id: expansionClip

            // `_active` lags `row.expanded` through the collapse animation by
            // also gating on `Layout.preferredHeight > 0` — Qt's animation
            // updates the height property directly during the transition,
            // so the value is positive while interpolating from full→0 and
            // only reaches 0 when the animation lands. Without this lag the
            // Loader would unload synchronously on collapse-start and the
            // user would see the viewport blank before the height finished
            // shrinking. Re-expanding mid-collapse keeps the Loader active
            // (no reload thrash) because the height never reaches 0.
            property bool _active: row.expandable && row.controller !== null && (row.expanded || Layout.preferredHeight > 0)

            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit * 2
            Layout.preferredHeight: row.expanded ? expansionLoader.implicitHeight + Kirigami.Units.smallSpacing : 0
            clip: true
            opacity: row.expanded ? 1 : 0
            visible: Layout.preferredHeight > 0 || opacity > 0

            Behavior on Layout.preferredHeight {
                PhosphorMotionAnimation {
                    profile: row.expanded ? "widget.accordionExpand" : "widget.accordionCollapse"
                    durationOverride: Kirigami.Units.shortDuration
                }
            }

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: row.expanded ? "widget.fadeIn" : "widget.fadeOut"
                    durationOverride: Kirigami.Units.veryShortDuration * 2
                }
            }

            // Lazy-loaded read-only preview of the rule's full match tree.
            // `active` gates instantiation on the row actually being expanded
            // — collapsed rows pay zero cost. The controller's `ruleJson`
            // returns a deep copy of the rule, so the View walks its own data
            // and a controller-side mutation won't yank the tree mid-render.
            Loader {
                id: expansionLoader

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                active: expansionClip._active
                visible: active
                sourceComponent: expansionComponent
            }
        }

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
}
