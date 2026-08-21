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
 * A leaf at the ROOT also gets Add condition / Add group buttons, which wrap
 * it into an `all` group holding the original condition plus the new one. The
 * guided starting points seed a single leaf as the whole match, so without
 * that promotion those rules could never take a second condition — see
 * `_promoteRootToGroup`.
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
    /// Widest operator label in pixels, measured once in RuleEditorBody and
    /// threaded down for the same reason `matchFieldOptions` is: the answer is
    /// field-independent, so every leaf measuring it produced one identical
    /// sweep per condition row. MatchLeafEditor sizes its operator dropdown to
    /// it, which is what keeps the operator column aligned across rows.
    required property real widestOperatorTextWidth
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
    /// No registered match fields ⇒ nothing for the user to ever pick, so
    /// adding a condition would hand them an empty field picker. Held here
    /// rather than inline on the buttons because both the composite block and
    /// the leaf-root promote row gate on it, and a guard that only half the
    /// add-paths honour is worse than no guard.
    readonly property bool _canAddCondition: matchEditor.matchFieldOptions.length > 0

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

    /// A blank condition. Deliberately fully unselected so the user must pick a
    /// field rather than being handed the first field by default:
    /// MatchLeafEditor renders the "Choose…" field picker and hides the
    /// operator / value editors until a field is chosen (at which point its
    /// onSelected seeds a valid operator and a typed value).
    /// `_matchHasFilledLeaves` gates save on a filled leaf, so an unfilled
    /// placeholder can't be saved.
    function _blankLeaf() {
        return {
            "field": "",
            "op": "",
            "value": ""
        };
    }

    function _addLeafChild() {
        var children = matchEditor._children.slice();
        children.push(matchEditor._blankLeaf());
        matchEditor._emitChildren(matchEditor._compositeKind, children);
    }

    function _addGroupChild() {
        var children = matchEditor._children.slice();
        children.push({
            "all": []
        });
        matchEditor._emitChildren(matchEditor._compositeKind, children);
    }

    /// Wrap a LEAF ROOT into an `all` group holding the existing condition plus
    /// @p sibling, and hand the new composite upward.
    ///
    /// Only the root can need this. Every seeded rule whose subject implies one
    /// condition — the Monitor / Desktop / Application / Activity start-from-
    /// scratch subjects and most quick-start templates — arrives as a bare leaf
    /// (`RuleTemplates::newEmptyRule` / `newRuleFromTemplate`), and a leaf
    /// renders no kind selector and no add-buttons. Without this the user could
    /// never give such a rule a second condition, which made every guided
    /// starting point a dead end the moment one condition wasn't enough.
    ///
    /// Doing it here rather than seeding `all:[leaf]` in the templates is what
    /// repairs rules ALREADY STORED with a leaf root, and it keeps a
    /// single-condition rule rendering as a plain row instead of a group
    /// wrapper the user never asked for.
    function _promoteRootToGroup(sibling) {
        matchEditor.nodeEdited({
            "all": [matchEditor.node, sibling]
        });
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
            widestOperatorTextWidth: matchEditor.widestOperatorTextWidth
            onLeafChanged: function (updated) {
                matchEditor.nodeEdited(updated);
            }
            onRemoveRequested: matchEditor.removeRequested()
        }
    }

    // ── Leaf ROOT: the promote-to-group affordance ──
    // A leaf CHILD needs none of this: its enclosing group already offers Add
    // condition / Add group, and a child that grew its own would be adding a
    // sibling to the wrong level. Only the root (`removable === false`) has no
    // group above it to ask.
    Loader {
        Layout.fillWidth: true
        active: matchEditor._isLeaf && !matchEditor.removable
        visible: active

        sourceComponent: RowLayout {
            spacing: Kirigami.Units.smallSpacing

            // Same leading gutter the composite header uses, so these buttons
            // line up with the field picker of the condition directly above
            // rather than hanging off the left edge.
            Item {
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
            }

            Button {
                text: i18n("Add condition")
                icon.name: "list-add"
                flat: true
                enabled: matchEditor._canAddCondition
                Accessible.name: i18n("Add a second condition, grouping it with this one")
                onClicked: matchEditor._promoteRootToGroup(matchEditor._blankLeaf())
            }

            Button {
                text: i18n("Add group")
                icon.name: "list-add"
                flat: true
                Accessible.name: i18n("Add a condition group, grouping it with this condition")
                onClicked: matchEditor._promoteRootToGroup({
                    "all": []
                })
            }

            Item {
                Layout.fillWidth: true
            }
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
                                    "widestOperatorTextWidth": Qt.binding(function () {
                                        return matchEditor.widestOperatorTextWidth;
                                    }),
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
                            enabled: matchEditor._canAddCondition
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
