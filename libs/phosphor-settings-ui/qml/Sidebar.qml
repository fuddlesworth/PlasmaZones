// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.settings.ui

/**
 * Page sidebar.
 *
 * Renders entries from the controller's PageRegistry as a list with two
 * navigation modes mixed freely:
 *
 *   - Drill-down parents (entry has children, not collapsible) — taps
 *     replace the list with the parent's children + a Back button.
 *
 *   - Inline-collapsible categories (entry has children, isCollapsible) —
 *     act as accordion headers within the current drill view; tapping
 *     toggles whether their children show below them, indented.
 *
 *   - Navigable leaves (entry has a qmlSource) — taps set
 *     controller.currentPageId.
 *
 * `currentParentId` is the drill scope; "" is top-level. The property is
 * externally bindable / settable so consumers can restore drill state on
 * startup (use `controller.parentChainFor(activePageId)` and drill into
 * the deepest ancestor).
 *
 * Type-as-you-search filters the flat-tree under the current drill scope
 * (or the whole catalogue when at top-level + no scope). Matches show
 * with their owning category prefixed ("Surfaces / Windows").
 */
QQC2.ScrollView {
    id: root

    required property ApplicationController controller
    //* Empty string means "showing top-level pages"; otherwise the parent id.
    property string currentParentId: ""
    /** Per-id expand state for inline-collapsible categories. Seeded true
     *  by default — the registry can override via setCollapsibleDefault
     *  later if a category should start collapsed. */
    property var expandedCategories: ({
    })
    /** Search text. Empty disables filtering and the sidebar drills /
     *  expands as normal. */
    property alias searchText: searchField.text
    /** Flat list cached + invalidated whenever the underlying state
     *  changes. Bound at the Repeater's model below. */
    property var _visible: _visibleItems()

    function drillInto(parentId) {
        root.currentParentId = parentId;
    }

    function drillOut() {
        root.currentParentId = "";
    }

    function toggleCategory(id) {
        const next = Object.assign({
        }, root.expandedCategories);
        next[id] = next[id] === false ? true : !(next[id] === true);
        // First-touch initialise to false on first toggle (default expanded).
        if (next[id] === undefined)
            next[id] = true;

        root.expandedCategories = next;
    }

    function _isExpanded(id) {
        // Default to expanded so categories open out of the box.
        return root.expandedCategories[id] !== false;
    }

    function _scopeChildren(parentId) {
        return root.controller.registry.childPagesData(parentId);
    }

    function _hasChildren(parentId) {
        return root.controller.registry.childPagesData(parentId).length > 0;
    }

    function _visibleItems() {
        // No search: walk children of currentParentId, splicing in
        // expanded collapsible categories' children inline.
        if (root.searchText.length === 0) {
            const out = [];
            const walk = function walk(parentId, depth) {
                const kids = root._scopeChildren(parentId);
                for (let i = 0; i < kids.length; ++i) {
                    const child = kids[i];
                    const childHasChildren = root._hasChildren(child.id);
                    const collapsible = child.isCollapsible === true && childHasChildren;
                    out.push({
                        "id": child.id,
                        "title": child.title,
                        "iconSource": child.iconSource,
                        "hasQmlSource": child.hasQmlSource === true,
                        "_depth": depth,
                        "_isCollapsibleHeader": collapsible,
                        "_isDrillParent": !collapsible && childHasChildren,
                        "_isExpanded": collapsible && root._isExpanded(child.id)
                    });
                    if (collapsible && root._isExpanded(child.id))
                        walk(child.id, depth + 1);

                }
            };
            walk(root.currentParentId, 0);
            return out;
        }
        // Search: flatten under scope, keep navigable leaves whose title
        // matches the query.
        const needle = root.searchText.toLowerCase();
        const matches = [];
        const collect = function collect(parentId, breadcrumb) {
            const kids = root._scopeChildren(parentId);
            for (let i = 0; i < kids.length; ++i) {
                const child = kids[i];
                const grand = root._hasChildren(child.id);
                if (grand) {
                    const next = breadcrumb.length === 0 ? child.title : breadcrumb + " / " + child.title;
                    collect(child.id, next);
                    continue;
                }
                if (!child.hasQmlSource)
                    continue;

                const label = (breadcrumb.length === 0 ? child.title : breadcrumb + " / " + child.title);
                if (label.toLowerCase().indexOf(needle) >= 0)
                    matches.push({
                        "id": child.id,
                        "title": label,
                        "iconSource": child.iconSource,
                        "hasQmlSource": true,
                        "_depth": 0,
                        "_isCollapsibleHeader": false,
                        "_isDrillParent": false,
                        "_isExpanded": false
                    });

            }
        };
        collect(root.currentParentId, "");
        return matches;
    }

    onCurrentParentIdChanged: _visible = _visibleItems()
    onExpandedCategoriesChanged: _visible = _visibleItems()
    onSearchTextChanged: _visible = _visibleItems()
    clip: true

    // Invalidate the cached visible list whenever any of its inputs changes.
    Connections {
        function onCurrentPageIdChanged() {
            root._visible = root._visibleItems();
        }

        target: root.controller
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: 0

        // ── Search field ────────────────────────────────────────────
        Kirigami.SearchField {
            id: searchField

            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            placeholderText: qsTr("Search settings…")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

        // ── Back button (only when drilled in) ──────────────────────
        QQC2.ItemDelegate {
            visible: root.currentParentId !== "" && root.searchText.length === 0
            Layout.fillWidth: true
            onClicked: root.drillOut()

            contentItem: RowLayout {
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Icon {
                    source: "go-previous-symbolic"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                }

                QQC2.Label {
                    Layout.fillWidth: true
                    text: qsTr("Back")
                }

            }

        }

        // ── Items ───────────────────────────────────────────────────
        Repeater {
            model: root._visible

            delegate: QQC2.ItemDelegate {
                id: itemDelegate

                required property var modelData
                readonly property bool isCurrent: !modelData._isCollapsibleHeader && modelData.hasQmlSource && root.controller.currentPageId === modelData.id

                Layout.fillWidth: true
                highlighted: isCurrent
                leftPadding: Kirigami.Units.smallSpacing + (modelData._depth * Kirigami.Units.gridUnit)
                onClicked: {
                    if (modelData._isCollapsibleHeader)
                        root.toggleCategory(modelData.id);
                    else if (modelData._isDrillParent)
                        root.drillInto(modelData.id);
                    else if (modelData.hasQmlSource)
                        root.controller.currentPageId = modelData.id;
                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    // Expand/collapse chevron for category headers; static
                    // icon for everything else.
                    Kirigami.Icon {
                        visible: modelData._isCollapsibleHeader
                        source: modelData._isExpanded ? "go-down-symbolic" : "go-next-symbolic"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        opacity: 0.7
                    }

                    Kirigami.Icon {
                        visible: !modelData._isCollapsibleHeader && modelData.iconSource !== ""
                        source: modelData.iconSource
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: modelData.title
                        font.weight: modelData._isCollapsibleHeader ? Font.DemiBold : Font.Normal
                    }

                    // Drill-in indicator for non-collapsible parents.
                    Kirigami.Icon {
                        visible: modelData._isDrillParent
                        source: "go-next-symbolic"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    }

                }

            }

        }

    }

}
