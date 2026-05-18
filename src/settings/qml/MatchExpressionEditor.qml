// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Recursive match-expression tree editor — the "WHEN" block.
 *
 * A match node is either a **leaf** `{ field, op, value }` or a **composite**
 * `{ all: [...] }` / `{ any: [...] }` / `{ none: [...] }`. This component
 * renders one node: a leaf delegates to MatchLeafEditor; a composite renders
 * a kind selector plus a child list, each child recursively a
 * MatchExpressionEditor.
 *
 * Because it instantiates itself, this file MUST be listed in the qml module's
 * QML_FILES — a missing entry is a runtime "not a type" load failure.
 *
 * Two-way: any edit emits `nodeChanged(updatedNode)`; the parent owns the tree.
 */
ColumnLayout {
    id: matchEditor

    /// The match node JSON — a leaf or a composite.
    required property var node
    /// The WindowRuleController, threaded down for leaf field/operator lists.
    required property var controller
    /// Recursion depth — drives the indentation guide.
    property int depth: 0
    /// True when this node may be removed (the root cannot).
    property bool removable: false
    readonly property bool _isLeaf: matchEditor.node && matchEditor.node.field !== undefined
    readonly property string _compositeKind: {
        if (!matchEditor.node)
            return "all";

        if (matchEditor.node.any !== undefined)
            return "any";

        if (matchEditor.node.none !== undefined)
            return "none";

        return "all";
    }
    readonly property var _children: matchEditor.node ? (matchEditor.node[matchEditor._compositeKind] || []) : []

    signal nodeChanged(var updatedNode)
    signal removeRequested()

    function _emitChildren(kind, children) {
        var next = {
        };
        next[kind] = children;
        matchEditor.nodeChanged(next);
    }

    function _replaceChild(index, updated) {
        var children = matchEditor._children.slice();
        children[index] = updated;
        matchEditor._emitChildren(matchEditor._compositeKind, children);
    }

    function _removeChild(index) {
        var children = matchEditor._children.slice();
        children.splice(index, 1);
        matchEditor._emitChildren(matchEditor._compositeKind, children);
    }

    function _addLeafChild() {
        var children = matchEditor._children.slice();
        children.push({
            "field": "appId",
            "op": "equals",
            "value": ""
        });
        matchEditor._emitChildren(matchEditor._compositeKind, children);
    }

    function _addGroupChild() {
        var children = matchEditor._children.slice();
        children.push({
            "all": []
        });
        matchEditor._emitChildren(matchEditor._compositeKind, children);
    }

    function _changeKind(newKind) {
        // Preserve the existing children when switching AND/OR/NOT.
        matchEditor._emitChildren(newKind, matchEditor._children);
    }

    spacing: Kirigami.Units.smallSpacing

    // ── Leaf node ──
    Loader {
        Layout.fillWidth: true
        active: matchEditor._isLeaf
        visible: active

        sourceComponent: MatchLeafEditor {
            node: matchEditor.node
            controller: matchEditor.controller
            onLeafChanged: function(updated) {
                matchEditor.nodeChanged(updated);
            }
            onRemoveRequested: matchEditor.removeRequested()
        }

    }

    // ── Composite node ──
    // Wrapped in a Loader so a leaf node never instantiates the (hidden)
    // composite kind selector, child Repeater and add-buttons subtree.
    Loader {
        Layout.fillWidth: true
        active: !matchEditor._isLeaf
        visible: active

        sourceComponent: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                // Indentation guide proportional to depth.
                Rectangle {
                    Layout.preferredWidth: matchEditor.depth * Kirigami.Units.largeSpacing
                    Layout.fillHeight: true
                    color: "transparent"
                }

                ComboBox {
                    id: kindCombo

                    Layout.preferredWidth: Kirigami.Units.gridUnit * 7
                    model: [{
                        "value": "all",
                        "label": i18nc("match composite — every child must match", "ALL of")
                    }, {
                        "value": "any",
                        "label": i18nc("match composite — at least one child must match", "ANY of")
                    }, {
                        "value": "none",
                        "label": i18nc("match composite — no child may match", "NONE of")
                    }]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: ["all", "any", "none"].indexOf(matchEditor._compositeKind)
                    Accessible.name: i18n("Condition group type")
                    onActivated: function(index) {
                        if (currentValue !== matchEditor._compositeKind)
                            matchEditor._changeKind(currentValue);

                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                ToolButton {
                    visible: matchEditor.removable
                    icon.name: "edit-delete"
                    ToolTip.text: i18n("Remove group")
                    ToolTip.visible: hovered
                    Accessible.name: i18n("Remove this condition group")
                    onClicked: matchEditor.removeRequested()
                }

            }

            // Children — each recursively a MatchExpressionEditor.
            Repeater {
                model: matchEditor._children.length

                MatchExpressionEditor {
                    required property int index

                    Layout.fillWidth: true
                    Layout.leftMargin: Kirigami.Units.largeSpacing
                    node: matchEditor._children[index]
                    controller: matchEditor.controller
                    depth: matchEditor.depth + 1
                    removable: true
                    onNodeChanged: function(updated) {
                        matchEditor._replaceChild(index, updated);
                    }
                    onRemoveRequested: matchEditor._removeChild(index)
                }

            }

            RowLayout {
                Layout.leftMargin: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing

                Button {
                    text: i18n("Add condition")
                    icon.name: "list-add"
                    flat: true
                    Accessible.name: i18n("Add a condition to this group")
                    onClicked: matchEditor._addLeafChild()
                }

                Button {
                    text: i18n("Add group")
                    icon.name: "list-add"
                    flat: true
                    Accessible.name: i18n("Add a nested condition group")
                    onClicked: matchEditor._addGroupChild()
                }

            }

        }

    }

}
