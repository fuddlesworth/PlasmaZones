// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.settings

/**
 * @brief Drag-reorderable, variable-height list of RuleRow delegates.
 *
 * Now a thin consumer of the generic ReorderableColumn: it supplies the rule
 * items, the id/reorderable accessors, the "rule:" deep-link anchor prefix, and
 * a RuleRow as the row content, and commits a drag/keyboard move by resolving
 * the drop neighbour and calling `controller.moveRule`. All the drag mechanics
 * (grip column, variable-height drop cascade, height cache, Alt+Up/Down) live
 * in ReorderableColumn, shared with the decoration ChainEditor.
 *
 * Public API (consumed by RulesPage) is unchanged.
 */
Item {
    id: root

    /// The rule entries — `[{ ruleId, name, enabled, ... }, ...]`.
    required property var rules
    /// The RuleController — supplies match-field/operator tables to each row's
    /// expansion view and owns `moveRule` for drag commits.
    required property var controller
    required property var matchFieldOptions
    required property var actionTypeOptions
    property var appSettings: null
    property bool reorderingEnabled: true
    property bool showSectionBadge: false

    /// Resolve the `beforeId` to hand `moveRule` for moving `list[from]` to slot
    /// `to`. `moveRule` inserts immediately BEFORE `beforeId` (empty = end).
    function beforeIdFor(list, from, to) {
        if (from < to)
            return to + 1 < list.length ? list[to + 1].ruleId : "";
        return list[to].ruleId;
    }

    signal toggleRequested(string ruleId, bool enabled)
    signal editRequested(string ruleId)
    signal duplicateRequested(string ruleId)
    signal deleteRequested(string ruleId)

    Layout.fillWidth: true
    Layout.preferredHeight: reorderList.totalHeight
    implicitHeight: reorderList.totalHeight

    ReorderableColumn {
        id: reorderList

        anchors.fill: parent
        items: root.rules
        reorderingEnabled: root.reorderingEnabled
        anchorPrefix: "rule:"
        idOf: function (item) {
            return item.ruleId;
        }
        accessibleNameOf: function (item) {
            return item.name || item.ruleId;
        }
        reorderableOf: function (item) {
            return item.managed !== true;
        }
        onMoveRequested: function (fromIndex, toIndex) {
            var snapshot = root.rules;
            if (fromIndex < 0 || fromIndex >= snapshot.length)
                return;
            var movedId = snapshot[fromIndex].ruleId;
            root.controller.moveRule(movedId, root.beforeIdFor(snapshot, fromIndex, toIndex));
        }

        rowDelegate: RuleRow {
            readonly property var _rule: parent.rowModelData

            ruleId: _rule.ruleId
            ruleName: _rule.name
            ruleEnabled: _rule.enabled
            matchSummary: _rule.matchSummary
            actionSummary: _rule.actionSummary
            conditionCount: _rule.conditionCount
            actionCount: _rule.actionCount
            isComposite: _rule.isComposite
            validationIssueCount: _rule.validationIssueCount
            priority: _rule.priority
            managed: _rule.managed === true
            sectionLabel: root.showSectionBadge ? (_rule.sectionLabel || "") : ""
            controller: root.controller
            matchFieldOptions: root.matchFieldOptions
            actionTypeOptions: root.actionTypeOptions
            appSettings: root.appSettings
            onToggleRequested: function (en) {
                root.toggleRequested(_rule.ruleId, en);
            }
            onEditRequested: root.editRequested(_rule.ruleId)
            onDuplicateRequested: root.duplicateRequested(_rule.ruleId)
            onDeleteRequested: root.deleteRequested(_rule.ruleId)
        }
    }
}
