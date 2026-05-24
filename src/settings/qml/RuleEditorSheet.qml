// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief The full rule editor — the "Advanced / Custom" editing surface.
 *
 * Edits one rule's name, enabled flag, priority, match-expression tree
 * (MatchExpressionEditor) and action list (ActionListEditor). Opened by
 * WindowRulesPage for an existing rule (edit) or by AddRuleSheet once the
 * user picks a subject (create).
 *
 * The sheet owns a working copy `_workingRule` so a Cancel discards cleanly;
 * Save emits `ruleSaved(ruleJson)` and the page commits it to the controller.
 */
Kirigami.OverlaySheet {
    // Wrap the ColumnLayout in a plain Item with explicit implicit
    // dimensions: that's the OverlaySheet-friendly way to force a minimum
    // content width without triggering its `implicitHeight` binding loop.
    // ColumnLayout overrides `implicitWidth/Height` to compute them from
    // its children on every layout pass, so setting `implicitWidth`
    // directly on ColumnLayout is silently overwritten — and any spacer
    // trick that re-introduces an implicit width via a child re-enters
    // the recompute loop together with the wrap-text labels below.

    id: sheet

    /// The WindowRuleController — threaded into the recursive editors.
    required property var controller
    /// The SettingsController — threaded into the leaf and action editors so
    /// they can offer screen / activity / layout pickers instead of raw text
    /// fields for opaque ids.
    required property var appSettings
    /// True when editing an existing rule (vs creating a new one).
    property bool editing: false
    /// Working copy of the rule being edited. Set via `openFor`.
    property var _workingRule: ({
    })
    /// Stable empty-match fallback — a single allocation, so binding
    /// MatchExpressionEditor.node to it does not churn the node identity on
    /// every binding evaluation.
    readonly property var _emptyMatch: ({
        "all": []
    })
    /// The controller's authoring metadata, cached once. `actionTypes()` and
    /// `matchFields()` are Q_INVOKABLEs that allocate a fresh QVariantList on
    /// every call — binding them directly would re-invoke (and churn the
    /// dependent Repeaters) on every unrelated `_workingRule` patch. Cache
    /// here and thread the cached lists down to the child editors.
    readonly property var _actionTypeOptions: sheet.controller.actionTypes()
    readonly property var _matchFieldOptions: sheet.controller.matchFields()
    /// Live semantic-validation pass over the working rule. Surfaced as an
    /// inline message and as a save gate so the user can't author a rule
    /// the picker would have prevented (e.g. by mutating the match after
    /// picking a context action). Recomputed on every working-rule patch.
    readonly property var _validationIssues: sheet.controller.validationIssuesForJson(sheet._workingRule)
    /// Save is allowed only when the rule has at least one action, every
    /// match leaf has a non-empty value, AND the semantic-validation pass
    /// found no action/match domain mismatches.
    readonly property bool _canSave: sheet._workingRule.actions !== undefined && sheet._workingRule.actions.length > 0 && sheet._matchHasFilledLeaves(sheet._workingRule.match) && sheet._validationIssues.length === 0

    signal ruleSaved(var ruleJson)

    /// Open the sheet to edit @p ruleJson (a JSON map). Pass `isEdit` false
    /// for a freshly-created rule.
    function openFor(ruleJson, isEdit) {
        // Deep-ish clone so Cancel can't leak mutations back to the model.
        sheet._workingRule = JSON.parse(JSON.stringify(ruleJson));
        sheet.editing = isEdit === true;
        sheet.open();
    }

    function _patch(key, value) {
        var next = JSON.parse(JSON.stringify(sheet._workingRule));
        next[key] = value;
        sheet._workingRule = next;
    }

    /// True if every leaf predicate in @p node carries a non-empty value.
    /// A leaf with an empty string / missing value would match an empty-string
    /// id (e.g. the guided `ScreenId == ""` seed) — block saving that.
    function _matchHasFilledLeaves(node) {
        if (!node)
            return true;

        // Leaf — `field` is the discriminator (mirrors MatchExpressionEditor).
        if (node.field !== undefined) {
            var v = node.value;
            if (v === undefined || v === null)
                return false;

            return String(v).length > 0;
        }
        // Composite — recurse into whichever child list is present.
        // An empty `all` is the legitimate catch-all (always-true) and stays
        // valid, but an empty `any` (matches nothing) or empty `none`
        // (matches everything) is a degenerate group — block saving it.
        var children;
        if (node.any !== undefined) {
            children = node.any;
            if (!children || children.length === 0)
                return false;

        } else if (node.none !== undefined) {
            children = node.none;
            if (!children || children.length === 0)
                return false;

        } else {
            children = node.all || [];
        }
        for (var i = 0; i < children.length; ++i) {
            if (!sheet._matchHasFilledLeaves(children[i]))
                return false;

        }
        return true;
    }

    title: sheet.editing ? i18n("Edit Window Rule") : i18n("New Window Rule")

    // The outer Item's `implicitWidth` is constant (36 gu), and its
    // `implicitHeight` tracks the column. The column anchors L/R to the
    // Item so its width is the fixed 36 gu, and its height is whatever
    // the children sum to — a clean acyclic chain.
    Item {
        id: contentRoot

        implicitWidth: Kirigami.Units.gridUnit * 36
        implicitHeight: column.implicitHeight

        ColumnLayout {
            // The persistent "first rule wins per slot" helper and the
            // save-gate / validation messages live in the sheet's `footer`
            // (defined below), NOT here. The OverlaySheet's footer is pinned
            // above the action buttons so the messages stay visible without
            // scrolling — on a smaller window, scroll-to-bottom would
            // otherwise clip them out of reach.

            id: column

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: Kirigami.Units.largeSpacing

            // ── Identity ──
            Kirigami.FormLayout {
                Layout.fillWidth: true

                TextField {
                    Kirigami.FormData.label: i18n("Name:")
                    text: sheet._workingRule.name || ""
                    placeholderText: i18n("Optional rule name")
                    Accessible.name: i18n("Rule name")
                    onEditingFinished: sheet._patch("name", text)
                }

                Switch {
                    Kirigami.FormData.label: i18n("Enabled:")
                    checked: sheet._workingRule.enabled !== false
                    Accessible.name: i18n("Rule enabled")
                    onToggled: sheet._patch("enabled", checked)
                }

                // Priority + band-name hint. Bare integers like "610" are
                // meaningless without the band scheme; the inline label maps the
                // current value back to its semantic band so users don't need to
                // memorise the cutoffs. Bands defined in
                // `windowrulecontroller.cpp` (`kAnimationBandBase = 100`,
                // `kApplicationBandBase = 200`, `kContextBandBase = 300`,
                // `kAdvancedBandBase = 500`).
                RowLayout {
                    id: priorityRow

                    readonly property int _priority: sheet._workingRule.priority || 0

                    Kirigami.FormData.label: i18n("Priority:")
                    spacing: Kirigami.Units.largeSpacing

                    SpinBox {
                        Accessible.name: i18n("Rule priority — higher rules are evaluated first")
                        // The band-name hint explains the number; attach the
                        // tooltip here (the value the user is editing)
                        // instead of on the read-only band Label — that's
                        // where users hover when they want to learn what
                        // "610" means.
                        ToolTip.delay: 500
                        ToolTip.text: i18n("Priority bands — 100: Animation, 200: Application, 300: Context, 500: Advanced. Higher numbers win within a band.")
                        ToolTip.visible: hovered
                        from: 0
                        to: 100000
                        value: priorityRow._priority
                        onValueModified: sheet._patch("priority", value)
                    }

                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        font.italic: true
                        opacity: 0.65
                        text: priorityRow._priority >= 500 ? i18n("Advanced") : priorityRow._priority >= 300 ? i18n("Context") : priorityRow._priority >= 200 ? i18n("Application") : priorityRow._priority >= 100 ? i18n("Animation") : i18n("Custom")
                    }

                }

            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // ── WHEN — the match-expression tree ──
            Label {
                text: i18n("WHEN")
                font.bold: true
                opacity: 0.7
                font.capitalization: Font.AllUppercase
            }

            MatchExpressionEditor {
                Layout.fillWidth: true
                node: sheet._workingRule.match || sheet._emptyMatch
                controller: sheet.controller
                appSettings: sheet.appSettings
                matchFieldOptions: sheet._matchFieldOptions
                depth: 0
                removable: false
                onNodeEdited: function(updated) {
                    sheet._patch("match", updated);
                }
            }

            Kirigami.Separator {
                Layout.fillWidth: true
            }

            // ── THEN — the action list ──
            ActionListEditor {
                Layout.fillWidth: true
                actions: sheet._workingRule.actions || []
                actionTypeOptions: sheet._actionTypeOptions
                appSettings: sheet.appSettings
                // True when the current match references only context fields
                // (ScreenId / VirtualDesktop / Activity) — drives the picker's
                // per-option compatibility flag so context-domain actions are
                // disabled with a tooltip when the match would render them
                // silently inert. Recomputed from the working rule's match on
                // every patch via the controller's Q_INVOKABLE.
                matchIsContextOnly: sheet.controller.matchIsContextOnly(sheet._workingRule.match || sheet._emptyMatch)
                onActionsEdited: function(updated) {
                    sheet._patch("actions", updated);
                }
            }

        }

    }

    footer: ColumnLayout {
        spacing: Kirigami.Units.smallSpacing

        // The "first rule wins per slot" helper — moved out of the
        // scrollable content so a tall rule (many conditions / actions)
        // can't push it out of view on a small window. Same anti-binding-loop
        // guard as before: `Layout.preferredWidth: 0` keeps the wrap-text
        // implicit width from re-entering the OverlaySheet's implicit
        // height binding chain.
        Label {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            wrapMode: Text.WordWrap
            font.italic: true
            opacity: 0.6
            text: i18n("The first rule (by priority) to fill each action slot wins that slot — actions in different slots stack.")
        }

        // Completeness gate — shown when Save is blocked for a reason that
        // is NOT a semantic validation issue (missing action / empty leaf).
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            type: Kirigami.MessageType.Information
            visible: !sheet._canSave && sheet._validationIssues.length === 0
            text: !sheet._workingRule.actions || sheet._workingRule.actions.length === 0 ? i18n("Add at least one action before saving.") : i18n("Every condition needs a value before this rule can be saved.")
        }

        // Semantic-validation surface — lifted out of the qCWarning log so
        // the user sees exactly why the rule would never fire. Save is also
        // gated on this list (see _canSave above).
        Kirigami.InlineMessage {
            Layout.fillWidth: true
            Layout.preferredWidth: 0
            type: Kirigami.MessageType.Warning
            visible: sheet._validationIssues.length > 0
            text: {
                if (sheet._validationIssues.length === 0)
                    return "";

                var lines = [];
                for (var i = 0; i < sheet._validationIssues.length; ++i) {
                    lines.push("• " + sheet._validationIssues[i].message);
                }
                return i18n("This rule would never fire:") + "\n" + lines.join("\n");
            }
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
                text: sheet.editing ? i18n("Save") : i18n("Add rule")
                icon.name: "dialog-ok-apply"
                // A rule with no actions does nothing, and a rule whose match has
                // an empty leaf value would match an empty-string id — block both.
                enabled: sheet._canSave
                Accessible.name: sheet.editing ? i18n("Save changes to this rule") : i18n("Add this rule")
                onClicked: {
                    sheet.ruleSaved(sheet._workingRule);
                    sheet.close();
                }
            }

        }

    }

}
