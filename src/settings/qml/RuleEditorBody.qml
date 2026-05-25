// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable rule-authoring form — name / enabled / priority / WHEN
 *        match tree / THEN action list.
 *
 * Lives inside both the edit OverlaySheet (`RuleEditorSheet`) and the create
 * wizard (`AddRuleWizard`) so the actual editing UI is defined exactly once.
 * Owns the working-rule mutation: callers seed `workingRule`, the body
 * mutates it in place via `_patch`, and consumers read back through the
 * `workingRule` property or attach the `canSave` / `validationIssues`
 * derivations onto their own footer / save button.
 *
 * The ScrollView contentItem mirrors `RuleEditorSheet`'s previous structure
 * — implicit height capped at 75% of the host window so a short rule
 * doesn't blow up the dialog while a tall rule scrolls inside the body.
 */
ScrollView {
    id: root

    /// The WindowRuleController — threaded into the recursive editors.
    required property var controller
    /// The SettingsController bridge — threaded into the leaf and action
    /// editors so the picker-kind params (screen / activity / layout) can
    /// populate their dropdowns instead of showing raw ids.
    required property var appSettings
    /// Working copy of the rule being edited. Consumers SET this when
    /// opening the editor; the body mutates it in place via _patch. Cancel
    /// in the consumer's footer should simply throw away its reference; we
    /// never alias the consumer's data.
    property var workingRule: ({
    })
    /// Stable empty-match fallback — a single allocation, so binding
    /// MatchExpressionEditor.node to it does not churn the node identity on
    /// every binding evaluation.
    readonly property var _emptyMatch: ({
        "all": []
    })
    /// Controller authoring metadata — cached once because
    /// actionTypes() / matchFields() allocate fresh QVariantLists on every
    /// call. Repeaters down the tree key off these so binding them through
    /// a property keeps the recompute fan-out tight.
    readonly property var _actionTypeOptions: root.controller.actionTypes()
    readonly property var _matchFieldOptions: root.controller.matchFields()
    /// Live semantic-validation pass over the working rule. Exposed so the
    /// consumer's footer can render an inline-message + gate its Save
    /// button — the wizard and the edit sheet both surface them.
    readonly property var validationIssues: root.controller.validationIssuesForJson(root.workingRule)
    /// True iff the working rule is structurally complete (≥1 action,
    /// every leaf filled) AND semantically clean (no action/match domain
    /// mismatches). The consumer's Save button binds its `enabled` to this.
    readonly property bool canSave: root.workingRule.actions !== undefined && root.workingRule.actions.length > 0 && root._matchHasFilledLeaves(root.workingRule.match) && root.validationIssues.length === 0

    function _patch(key, value) {
        var next = JSON.parse(JSON.stringify(root.workingRule));
        next[key] = value;
        root.workingRule = next;
    }

    /// True if every leaf predicate in @p node carries a non-empty value.
    /// A leaf with an empty string / missing value would match an empty-string
    /// id (e.g. the guided `ScreenId == ""` seed) — block saving that.
    function _matchHasFilledLeaves(node) {
        if (!node)
            return true;

        if (node.field !== undefined) {
            var v = node.value;
            if (v === undefined || v === null)
                return false;

            return String(v).length > 0;
        }
        // Composite — recurse into whichever child list is present. Empty
        // `all` is the legitimate catch-all (always-true), but empty `any`
        // (matches nothing) and empty `none` (matches everything) are
        // degenerate — block save.
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
            if (!root._matchHasFilledLeaves(children[i]))
                return false;

        }
        return true;
    }

    // The 36 gu implicitWidth establishes the editor's minimum width; the
    // inner column binds its width to `availableWidth` (ScrollView's
    // viewport width minus the scrollbar) so wrap-text widgets compute
    // their wrapped heights against a stable width without re-entering an
    // implicit-width feedback loop with the hosting sheet/dialog.
    implicitWidth: Kirigami.Units.gridUnit * 36
    // Cap the implicit height at 75% of the host window so a short rule
    // doesn't take screen-height worth of vertical room while a tall rule
    // still scrolls inside the body. The window-height fallback handles
    // the brief pre-attach window where the parent chain hasn't resolved.
    implicitHeight: Math.min(column.implicitHeight, root.Window.window ? root.Window.window.height * 0.75 : column.implicitHeight)
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ScrollBar.vertical.policy: ScrollBar.AsNeeded

    ColumnLayout {
        id: column

        width: root.availableWidth
        spacing: Kirigami.Units.largeSpacing

        // ── Identity ──
        Kirigami.FormLayout {
            Layout.fillWidth: true

            TextField {
                Kirigami.FormData.label: i18n("Name:")
                text: root.workingRule.name || ""
                placeholderText: i18n("Optional rule name")
                Accessible.name: i18n("Rule name")
                onEditingFinished: root._patch("name", text)
            }

            Switch {
                Kirigami.FormData.label: i18n("Enabled:")
                checked: root.workingRule.enabled !== false
                Accessible.name: i18n("Rule enabled")
                onToggled: root._patch("enabled", checked)
            }

            // Priority + band-name hint. Bare integers like "610" are
            // meaningless without the band scheme; the inline label maps the
            // current value back to its semantic band. Bands defined in
            // windowrulecontroller.cpp.
            RowLayout {
                id: priorityRow

                readonly property int _priority: root.workingRule.priority || 0

                Kirigami.FormData.label: i18n("Priority:")
                spacing: Kirigami.Units.largeSpacing

                SpinBox {
                    Accessible.name: i18n("Rule priority — higher rules are evaluated first")
                    ToolTip.delay: 500
                    ToolTip.text: i18n("Priority bands — 100: Animation, 200: Application, 300: Context, 500: Advanced. Higher numbers win within a band.")
                    ToolTip.visible: hovered
                    from: 0
                    to: 100000
                    value: priorityRow._priority
                    onValueModified: root._patch("priority", value)
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
            node: root.workingRule.match || root._emptyMatch
            controller: root.controller
            appSettings: root.appSettings
            matchFieldOptions: root._matchFieldOptions
            depth: 0
            removable: false
            onNodeEdited: function(updated) {
                root._patch("match", updated);
            }
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── THEN — the action list ──
        ActionListEditor {
            Layout.fillWidth: true
            actions: root.workingRule.actions || []
            actionTypeOptions: root._actionTypeOptions
            appSettings: root.appSettings
            matchIsContextOnly: root.controller.matchIsContextOnly(root.workingRule.match || root._emptyMatch)
            onActionsEdited: function(updated) {
                root._patch("actions", updated);
            }
        }

    }

}
