// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.settings.ui

/**
 * Page sidebar.
 *
 * Vertical layout:
 *
 *   ┌─────────────────────────┐
 *   │ SearchField  (sticky)   │
 *   ├─────────────────────────┤
 *   │ Back button (when drilled)
 *   │ ListView (scrollable)   │
 *   │ — rows drill / toggle / │
 *   │   navigate as below     │
 *   ├─────────────────────────┤
 *   │ footerContent (sticky)  │
 *   └─────────────────────────┘
 *
 * Navigation modes:
 *
 *   - Drill-down parents (entry has children, not collapsible) — taps
 *     replace the list with the parent's children + a Back button.
 *   - Inline-collapsible categories (entry has children, isCollapsible) —
 *     accordion headers; tapping toggles children. Toggling animates
 *     rows in/out via ListView add/remove Transitions.
 *   - Navigable leaves (entry has a qmlSource) — taps set
 *     controller.currentPageId.
 *
 * Slots for consumers:
 *
 *   - `trailingDelegate`: Component instantiated next to each row's
 *     title (between label and drill chevron). Used by PlasmaZones for
 *     the snapping/tiling Switch + dirty badge.
 *   - `footerContent`: Component instantiated at the very bottom of
 *     the sidebar, OUTSIDE the scroll area. Stays visible across
 *     drill / scroll. Used by PlasmaZones for the persistent daemon
 *     status + enable/disable toggle.
 */
ColumnLayout {
    id: root

    required property ApplicationController controller
    //* Empty string means "showing top-level pages"; otherwise the parent id.
    property string currentParentId: ""
    /** Per-id expand state for inline-collapsible categories. Default
     *  expanded (true). Flip an entry to false to start it collapsed. */
    property var expandedCategories: ({
    })
    //* Search text. Empty disables filtering.
    property alias searchText: searchField.text
    /** Optional Component instantiated next to each row's title. The
     *  loader exposes the row's entry as `modelData`. */
    property Component trailingDelegate: null
    /** Optional Component instantiated at the bottom of the sidebar,
     *  below the scrollable list. Stays visible while the list
     *  scrolls. Used for persistent status surfaces (e.g. a daemon
     *  enable/disable toggle that should be reachable from every
     *  page). */
    property Component footerContent: null
    /** Suppress per-row add/remove animations while the whole list is
     *  cross-fading on drill-in/out. */
    property bool _suppressAccordion: false

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

    function _refreshModel() {
        const wanted = root._visibleItems();
        const wantedIds = new Set(wanted.map((w) => {
            return w.id;
        }));
        for (let i = visibleModel.count - 1; i >= 0; --i) {
            if (!wantedIds.has(visibleModel.get(i).id))
                visibleModel.remove(i);

        }
        for (let i = 0; i < wanted.length; ++i) {
            const item = wanted[i];
            if (i < visibleModel.count && visibleModel.get(i).id === item.id) {
                visibleModel.set(i, item);
                continue;
            }
            let currentIdx = -1;
            for (let j = i; j < visibleModel.count; ++j) {
                if (visibleModel.get(j).id === item.id) {
                    currentIdx = j;
                    break;
                }
            }
            if (currentIdx === -1) {
                visibleModel.insert(i, item);
            } else {
                visibleModel.move(currentIdx, i, 1);
                visibleModel.set(i, item);
            }
        }
    }

    spacing: 0
    onCurrentParentIdChanged: _refreshModel()
    onExpandedCategoriesChanged: _refreshModel()
    onSearchTextChanged: _refreshModel()
    Component.onCompleted: _refreshModel()

    Connections {
        function onCurrentPageIdChanged() {
            root._refreshModel();
        }

        target: root.controller
    }

    SequentialAnimation {
        id: drillAnimation

        property string pendingParentId: ""

        ScriptAction {
            script: root._suppressAccordion = true
        }

        PhosphorMotionAnimation {
            target: listColumn
            properties: "opacity"
            to: 0
            profile: "panel.fadeOut"
        }

        ScriptAction {
            script: root.currentParentId = drillAnimation.pendingParentId
        }

        PhosphorMotionAnimation {
            target: listColumn
            properties: "opacity"
            to: 1
            profile: "panel.fadeIn"
        }

        ScriptAction {
            script: root._suppressAccordion = false
        }

    }

    ListModel {
        id: visibleModel
    }

    // ── Sticky search field at the top ──────────────────────────────
    Kirigami.SearchField {
        id: searchField

        Layout.fillWidth: true
        Layout.margins: Kirigami.Units.smallSpacing
        placeholderText: qsTr("Search settings…")
    }

    Kirigami.Separator {
        Layout.fillWidth: true
    }

    // ── Scrollable list area ────────────────────────────────────────
    QQC2.ScrollView {
        id: listScroll

        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ColumnLayout {
            id: listColumn

            width: listScroll.availableWidth
            spacing: 0

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

            ListView {
                id: listView

                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                model: visibleModel
                interactive: false
                spacing: 0

                add: Transition {
                    enabled: !root._suppressAccordion

                    PhosphorMotionAnimation {
                        properties: "opacity"
                        from: 0
                        to: 1
                        profile: "widget.accordionExpand"
                    }

                    PhosphorMotionAnimation {
                        properties: "y"
                        profile: "widget.accordionExpand"
                    }

                }

                remove: Transition {
                    enabled: !root._suppressAccordion

                    PhosphorMotionAnimation {
                        properties: "opacity"
                        from: 1
                        to: 0
                        profile: "widget.accordionCollapse"
                    }

                }

                displaced: Transition {
                    enabled: !root._suppressAccordion

                    PhosphorMotionAnimation {
                        properties: "y"
                        profile: "widget.accordionExpand"
                    }

                }

                delegate: QQC2.ItemDelegate {
                    id: itemDelegate

                    required property string id
                    required property string title
                    required property string iconSource
                    required property bool hasQmlSource
                    required property int _depth
                    required property bool _isCollapsibleHeader
                    required property bool _isDrillParent
                    required property bool _isExpanded
                    readonly property var entryData: ({
                        "id": id,
                        "title": title,
                        "iconSource": iconSource,
                        "hasQmlSource": hasQmlSource,
                        "_depth": _depth,
                        "_isCollapsibleHeader": _isCollapsibleHeader,
                        "_isDrillParent": _isDrillParent,
                        "_isExpanded": _isExpanded
                    })
                    readonly property bool isCurrent: !_isCollapsibleHeader && hasQmlSource && root.controller.currentPageId === id

                    width: ListView.view.width
                    highlighted: isCurrent
                    leftPadding: Kirigami.Units.smallSpacing + (_depth * Kirigami.Units.gridUnit)
                    onClicked: {
                        if (_isCollapsibleHeader)
                            root.toggleCategory(id);
                        else if (_isDrillParent)
                            root.drillInto(id);
                        else if (hasQmlSource)
                            root.controller.currentPageId = id;
                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Icon {
                            visible: itemDelegate._isCollapsibleHeader
                            source: itemDelegate._isExpanded ? "go-down-symbolic" : "go-next-symbolic"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            opacity: 0.7
                        }

                        Kirigami.Icon {
                            visible: !itemDelegate._isCollapsibleHeader && itemDelegate.iconSource !== ""
                            source: itemDelegate.iconSource
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            elide: Text.ElideRight
                            text: itemDelegate.title
                            font.weight: itemDelegate._isCollapsibleHeader ? Font.DemiBold : Font.Normal
                        }

                        Loader {
                            id: trailingLoader

                            property var modelData: itemDelegate.entryData

                            sourceComponent: root.trailingDelegate
                            active: root.trailingDelegate !== null
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Kirigami.Icon {
                            visible: itemDelegate._isDrillParent
                            source: "go-next-symbolic"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        }

                    }

                }

            }

        }

    }

    // ── Sticky footer slot (e.g. daemon status / enable toggle) ─────
    Loader {
        id: footerLoader

        Layout.fillWidth: true
        // Layout.preferredWidth: 0 stops the loaded item's
        // implicitWidth (a Pane wrapping a RowLayout of dot + label +
        // Switch can be wide) from cascading up through the Sidebar's
        // ColumnLayout and inflating the whole sidebar past its
        // preferredWidth. fillWidth still sizes us to the column's
        // assigned width.
        Layout.preferredWidth: 0
        Layout.preferredHeight: item ? item.implicitHeight : 0
        active: root.footerContent !== null
        visible: active
        sourceComponent: root.footerContent
        onLoaded: {
            if (item)
                item.width = Qt.binding(() => {
                return footerLoader.width;
            });

        }
    }

}
