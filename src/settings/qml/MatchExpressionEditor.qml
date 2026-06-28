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
 * Two-way: any edit emits `nodeEdited(updatedNode)`; the parent owns the tree.
 */
ColumnLayout {
    id: matchEditor

    /// The match node JSON — a leaf or a composite.
    required property var node
    /// The RuleController, threaded down for leaf field/operator lists.
    required property var controller
    /// The SettingsController, threaded into MatchLeafEditor so picker-kind
    /// leaves (screen / activity) can populate their dropdowns.
    required property var appSettings
    /// Cached `controller.matchFields()` from RuleEditorSheet — the Q_INVOKABLE
    /// allocates a fresh list per call, so it is cached once at the root and
    /// threaded down rather than re-invoked here.
    required property var matchFieldOptions
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

    // `nodeEdited`, not `nodeChanged`, because `property var node` already
    // auto-generates a `nodeChanged()` change-signal and QML rejects the
    // duplicate. This signal carries the updated subtree upward to the parent.
    signal nodeEdited(var updatedNode)
    signal removeRequested

    function _emitChildren(kind, children) {
        var next = {};
        next[kind] = children;
        matchEditor.nodeEdited(next);
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
        // Derive the starting leaf from the controller's authoring metadata so
        // QML never reconstructs the field/operator wire table itself. The
        // "Add condition" button is gated on a non-empty list, so [0] is safe.
        var firstField = matchEditor.matchFieldOptions[0];
        var operators = matchEditor.controller.operatorsForField(firstField.value);
        var children = matchEditor._children.slice();
        children.push({
            "field": firstField.wire,
            "op": operators[0].wire,
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
            appSettings: matchEditor.appSettings
            fieldOptions: matchEditor.matchFieldOptions
            onLeafChanged: function (updated) {
                matchEditor.nodeEdited(updated);
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

                // Leading gutter the width of a leaf row's info icon, so this
                // group's kind selector lines up with the field pickers of the
                // sibling leaf rows at the same level — every primary control
                // forms one column per nesting level. Depth is conveyed by the
                // children block's indent + vertical rail below, not by a
                // per-header spacer (that compounded with the child indent and
                // inverted the nesting past depth 1).
                Item {
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                }

                WideComboBox {
                    id: kindCombo

                    model: [
                        {
                            "value": "all",
                            "label": i18nc("composite match where every child must match", "ALL of")
                        },
                        {
                            "value": "any",
                            "label": i18nc("composite match where at least one child must match", "ANY of")
                        },
                        {
                            "value": "none",
                            "label": i18nc("composite match where no child may match", "NONE of")
                        }
                    ]
                    textRole: "label"
                    valueRole: "value"
                    currentIndex: ["all", "any", "none"].indexOf(matchEditor._compositeKind)
                    Accessible.name: i18n("Condition group type")
                    onActivated: function (index) {
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

            // Indented children block with a vertical guide rail down its left
            // edge. The rail (a full-height Separator) makes each nesting level
            // legible at a glance; this row's leftMargin + spacing is the SINGLE
            // source of indentation, composing correctly through the recursion.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.smallSpacing
                spacing: Kirigami.Units.largeSpacing

                // Vertical nesting rail. fillHeight stretches it to the sibling
                // column's height (the column carries the real implicit height,
                // so the rail just fills — it never drives the height solve).
                Kirigami.Separator {
                    Layout.fillHeight: true
                    Layout.preferredWidth: 1
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    // Children — each recursively a MatchExpressionEditor, loaded
                    // by URL via Loader. Qt 6 forbids direct self-instantiation of
                    // a type from within its own definition (the type isn't fully
                    // registered yet at compile time → "instantiated recursively"
                    // → unavailable). The URL-based Loader defers resolution to
                    // runtime; setSource's initialProperties seed the inner
                    // `required` properties, and a Qt.binding on `node` keeps it
                    // reactive to _replaceChild updates.
                    Repeater {
                        model: matchEditor._children.length

                        Loader {
                            id: childLoader

                            required property int index

                            Layout.fillWidth: true
                            Component.onCompleted: {
                                setSource("MatchExpressionEditor.qml", {
                                    "node": Qt.binding(function () {
                                        return matchEditor._children[childLoader.index];
                                    }),
                                    "controller": matchEditor.controller,
                                    "appSettings": matchEditor.appSettings,
                                    "matchFieldOptions": matchEditor.matchFieldOptions,
                                    "removable": true
                                });
                            }

                            Connections {
                                function onNodeEdited(updated) {
                                    matchEditor._replaceChild(childLoader.index, updated);
                                }

                                function onRemoveRequested() {
                                    matchEditor._removeChild(childLoader.index);
                                }

                                target: childLoader.item
                            }
                        }
                    }

                    RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Button {
                            text: i18n("Add condition")
                            icon.name: "list-add"
                            flat: true
                            // No registered match fields ⇒ nothing to seed a leaf
                            // from; disable rather than dereferencing an empty list.
                            enabled: matchEditor.matchFieldOptions.length > 0
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
    }
}
