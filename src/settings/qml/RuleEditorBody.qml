// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable rule-authoring form — name / enabled / priority / WHEN
 *        match tree / THEN action list.
 *
 * Lives inside both the edit dialog (`RuleEditorSheet`) and the create wizard
 * (`AddRuleWizard`) — both `Kirigami.Dialog` — so the actual editing UI is
 * defined exactly once. Owns the working-rule mutation: callers seed
 * `workingRule`, the body mutates it in place via `_patch`, and consumers read
 * back through the `workingRule` property or attach the `canSave` /
 * `validationIssues` derivations onto their own footer / save button.
 *
 * This is a plain ColumnLayout, NOT a ScrollView: Kirigami.Dialog already wraps
 * its content in its own ScrollView, so a tall rule scrolls inside the host;
 * nesting another scroller here would just double up.
 */
ColumnLayout {
    id: root

    /// The RuleController — threaded into the recursive editors.
    required property var controller
    /// The SettingsController bridge — threaded into the leaf and action
    /// editors so the picker-kind params (screen / activity / layout) can
    /// populate their dropdowns instead of showing raw ids.
    required property var appSettings
    /// Working copy of the rule being edited. Consumers BIND this to
    /// their staging state (e.g. `workingRule: sheet._workingRule`); the
    /// body emits `workingRuleEdited(next)` on every field change and
    /// the host updates its staging value, which the binding then
    /// propagates back into this property — preserving the binding
    /// instead of breaking it.
    ///
    /// The previous shape did `root.workingRule = next` inside `_patch`,
    /// which BROKE the binding on the first edit. Subsequent host
    /// re-seeds (e.g. `RuleEditorSheet.openFor(...)` after a Save) then
    /// failed to propagate, leaving the body stuck on the previously-
    /// edited rule. Signal-based propagation keeps the host as the
    /// single source of truth.
    property var workingRule: ({})
    /// Emitted whenever the body mutates a field. The host should
    /// update its staging state from `next` (the host's binding into
    /// `workingRule` then propagates the new value back into us).
    signal workingRuleEdited(var next)
    /// Stable empty-match fallback — a single allocation, so binding
    /// MatchExpressionEditor.node to it does not churn the node identity on
    /// every binding evaluation.
    readonly property var _emptyMatch: ({
            "all": []
        })
    /// Controller authoring metadata — cached on construction because
    /// actionTypes() / matchFields() allocate fresh QVariantLists on every
    /// call. Repeaters down the tree key off these so binding them through
    /// a property keeps the recompute fan-out tight. The catalogue
    /// invalidation signal lets a plugin-driven controller refresh the
    /// cache at runtime; static-catalogue controllers (the common case)
    /// just never fire it.
    property var _actionTypeOptions: root.controller.actionTypes()
    property var _matchFieldOptions: root.controller.matchFields()

    Connections {
        function onAuthoringCatalogueChanged() {
            root._actionTypeOptions = root.controller.actionTypes();
            root._matchFieldOptions = root.controller.matchFields();
        }
        target: root.controller
        ignoreUnknownSignals: true
    }
    /// Debounced cache of the most recent validation pass. The binding
    /// below recomputes on every keystroke (workingRule churns through
    /// JSON clones from `_patch`); the controller's
    /// `validationIssuesForJson` walks the full match tree + action list
    /// and is non-trivial on a tall rule. Debouncing through a 50 ms
    /// Timer collapses bursts of edits (held key, fast paste) into one
    /// validation pass without making the status-bar feel laggy.
    property var validationIssues: []

    onWorkingRuleChanged: validationTimer.restart()
    Component.onCompleted: {
        // Seed the cache so the consumer's footer doesn't flash a
        // "no issues" state on first paint before the 50 ms timer fires.
        validationIssues = root.controller.validationIssuesForJson(root.workingRule);
    }

    Timer {
        id: validationTimer

        interval: 50
        repeat: false
        onTriggered: root.validationIssues = root.controller.validationIssuesForJson(root.workingRule)
    }
    /// True iff the working rule is structurally complete (≥1 action,
    /// every leaf filled) AND semantically clean (no action/match domain
    /// mismatches). The consumer's Save button binds its `enabled` to this.
    readonly property bool canSave: root.workingRule.actions !== undefined && root.workingRule.actions.length > 0 && root._matchHasFilledLeaves(root.workingRule.match) && root.validationIssues.length === 0

    function _patch(key, value) {
        // Shallow clone via Object.assign — we replace `key`'s value
        // wholesale on the next line. A deep clone (JSON.parse +
        // JSON.stringify) would be O(rule-size) on every text-field
        // edit; shallow is O(top-level-keys) and equivalent for this
        // use case.
        //
        // Emit the edited copy via workingRuleEdited rather than
        // assigning back to root.workingRule — direct assignment
        // breaks the host's `workingRule: host._workingRule` binding,
        // and subsequent host re-seeds would never reach us.
        var next = Object.assign({}, root.workingRule);
        next[key] = value;
        root.workingRuleEdited(next);
    }

    /// Hard recursion-depth cap. A malformed or malicious rule with a
    /// cyclic composite (or just absurdly nested) would otherwise
    /// stack-overflow on every keystroke through `canSave`. 64 is
    /// generous for any human-authored rule.
    readonly property int _maxMatchDepth: 64

    /// True if every leaf predicate in @p node carries a non-empty value.
    /// A leaf with an empty string / missing value would match an empty-string
    /// id (e.g. the guided `ScreenId == ""` seed) — block saving that.
    function _matchHasFilledLeaves(node, depth) {
        if (depth === undefined)
            depth = 0;
        if (depth > root._maxMatchDepth)
            return false;
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
            if (!root._matchHasFilledLeaves(children[i], depth + 1))
                return false;
        }
        return true;
    }

    spacing: Kirigami.Units.largeSpacing
    // 36gu preferred width gives the editor a comfortable form width. Both
    // hosts — the edit RuleEditorSheet and the create AddRuleWizard, both
    // Kirigami.Dialog — read this to size themselves and each wraps its content
    // in its own scroller, so this body is a plain ColumnLayout (not a
    // ScrollView): a nested scroller would just double up. Hosts that place the
    // body inside a layout add Layout.fillWidth to stretch it to their width.
    Layout.preferredWidth: Kirigami.Units.gridUnit * 36

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

        SettingsSwitch {
            Kirigami.FormData.label: i18n("Enabled:")
            checked: root.workingRule.enabled !== false
            accessibleName: i18n("Rule enabled")
            onToggled: function (newValue) {
                root._patch("enabled", newValue);
            }
        }

        // Priority + band-name hint. Bare integers like "610" are
        // meaningless without the band scheme; the inline label maps the
        // current value back to its semantic band. Bands defined in
        // rulecontroller.cpp.
        RowLayout {
            id: priorityRow

            readonly property int _priority: root.workingRule.priority || 0

            Kirigami.FormData.label: i18n("Priority:")
            spacing: Kirigami.Units.largeSpacing

            SpinBox {
                Accessible.name: i18n("Rule priority. Higher rules are evaluated first.")
                ToolTip.delay: 500
                ToolTip.text: i18n("Priority bands. 100: Animation, 200: Application, 300: Context, 500: Advanced. Higher numbers win within a band.")
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
        removable: false
        onNodeEdited: function (updated) {
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
        controller: root.controller
        actionTypeOptions: root._actionTypeOptions
        appSettings: root.appSettings
        matchIsContextOnly: root.controller.matchIsContextOnly(root.workingRule.match || root._emptyMatch)
        onActionsEdited: function (updated) {
            root._patch("actions", updated);
        }
    }
}
