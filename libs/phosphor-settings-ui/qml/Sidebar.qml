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
 *
 * Drill transitions (`drillInto` / `drillOut`) cross-fade the list — fade
 * out the current items, swap currentParentId, fade in the new items.
 *
 * Per-row trailing content can be injected via `trailingDelegate`: a QML
 * Component instantiated next to each row's title. The loader exposes
 * the row's entry as `modelData` so consumers can branch on `id`. Used
 * by PlasmaZones to put inline Switch widgets next to "snapping" and
 * "tiling" rows for quick feature toggling.
 */
QQC2.ScrollView {
    id: root

    required property ApplicationController controller
    //* Empty string means "showing top-level pages"; otherwise the parent id.
    property string currentParentId: ""
    /** Per-id expand state for inline-collapsible categories. Seeded true
     *  by default — flip an entry to false to start it collapsed. */
    property var expandedCategories: ({
    })
    /** Search text. Empty disables filtering and the sidebar drills /
     *  expands as normal. */
    property alias searchText: searchField.text
    /** Optional Component instantiated next to each row's title (between
     *  the label and the drill chevron). The Component's root item sees
     *  the entry via `parent.modelData` (id, title, iconSource, etc.).
     *  When null (default) the slot collapses to nothing. */
    property Component trailingDelegate: null
    property var _visible: _visibleItems()

    function drillInto(parentId) {
        if (root.currentParentId === parentId)
            return ;

        drillAnimation.pendingParentId = parentId;
        drillAnimation.restart();
    }

    function drillOut() {
        if (root.currentParentId === "")
            return ;

        drillAnimation.pendingParentId = "";
        drillAnimation.restart();
    }

    function toggleCategory(id) {
        const next = Object.assign({
        }, root.expandedCategories);
        next[id] = next[id] === false ? true : !(next[id] === true);
        if (next[id] === undefined)
            next[id] = true;

        root.expandedCategories = next;
    }

    function _isExpanded(id) {
        return root.expandedCategories[id] !== false;
    }

    function _scopeChildren(parentId) {
        return root.controller.registry.childPagesData(parentId);
    }

    function _hasChildren(parentId) {
        return root.controller.registry.childPagesData(parentId).length > 0;
    }

    function _visibleItems() {
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

    Connections {
        function onCurrentPageIdChanged() {
            root._visible = root._visibleItems();
        }

        target: root.controller
    }

    // Drill-in / drill-out cross-fade. Fades the inner column out, swaps
    // currentParentId at zero opacity, then fades back in. Matches the
    // legacy `sidebarTransition` UX.
    SequentialAnimation {
        id: drillAnimation

        property string pendingParentId: ""

        NumberAnimation {
            target: listColumn
            property: "opacity"
            to: 0
            duration: Kirigami.Units.shortDuration
            easing.type: Easing.OutQuad
        }

        ScriptAction {
            script: root.currentParentId = drillAnimation.pendingParentId
        }

        NumberAnimation {
            target: listColumn
            property: "opacity"
            to: 1
            duration: Kirigami.Units.shortDuration * 1.5
            easing.type: Easing.InQuad
        }

    }

    ColumnLayout {
        id: listColumn

        width: root.availableWidth
        spacing: 0

        Kirigami.SearchField {
            id: searchField

            Layout.fillWidth: true
            Layout.margins: Kirigami.Units.smallSpacing
            placeholderText: qsTr("Search settings…")
        }

        Kirigami.Separator {
            Layout.fillWidth: true
        }

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

                    // Per-row trailing slot. The Loader exposes `modelData`
                    // (the entry data) so consumers can branch on id.
                    Loader {
                        id: trailingLoader

                        property var modelData: itemDelegate.modelData

                        sourceComponent: root.trailingDelegate
                        active: root.trailingDelegate !== null
                        Layout.alignment: Qt.AlignVCenter
                    }

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
