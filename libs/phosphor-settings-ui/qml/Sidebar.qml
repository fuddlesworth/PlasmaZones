// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.settings.ui
import "LoaderHelpers.js" as PhosphorLoaderHelpers

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
    // Flip relative to the *displayed* state — _isExpanded() treats
    // an absent id as expanded (the rail starts open), so the
    // toggle must respect that view. The earlier ternary form
    // mapped undefined → true (no-op for the user) and required a
    // second click to actually collapse a never-toggled category.

    id: root

    required property ApplicationController controller
    /** When true the sidebar collapses to an icon-only rail: search
     *  field hidden, row labels / chevrons / trailing widgets hidden
     *  (icons centred), and tooltips show on hover so the labels are
     *  still discoverable. Driven by SettingsAppWindow's
     *  `sidebarCompact` property by default. Standalone consumers can
     *  set this directly. */
    property bool compact: false
    //* Empty string means "showing top-level pages"; otherwise the parent id.
    property string currentParentId: ""
    /** Per-id expand state for inline-collapsible categories. Default
     *  expanded (true). Flip an entry to false to start it collapsed.
     *
     *  Initialised via `Object.create(null)` (not `{}`) so page ids that
     *  collide with JS built-in property names (`toString`, `constructor`,
     *  `hasOwnProperty`, etc.) read as `undefined` instead of inheriting
     *  the prototype's truthy method. Without this defensive form,
     *  `_isExpanded("toString")` would read the inherited function
     *  reference, treat it as `!== false`, and report expanded
     *  regardless of the toggle state. */
    property var expandedCategories: Object.create(null)
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
    // Legacy row-height multipliers — extracted from inline magic numbers
    // to a single source so a future row-density tweak touches one place.
    readonly property real backButtonHeight: Kirigami.Units.gridUnit * 2.6
    readonly property real navRowHeight: Kirigami.Units.gridUnit * 2.2
    // Active-row left-accent stripe width — Math.round so fractional DPRs
    // (1.5×, 1.25×) don't yield sub-pixel widths that anti-alias to a
    // washed-out half-pixel line.
    readonly property int accentBarWidth: Math.round(Kirigami.Units.devicePixelRatio * 2.5)

    function drillInto(parentId) {
        // Short-circuit on either the already-displayed scope OR a
        // drill already in flight targeting the same parent — without
        // the second check, a double-click on a drill row would
        // restart the cross-fade mid-animation and the user would see
        // the panel briefly flash back to opacity 0.
        if (root.currentParentId === parentId)
            return;

        if (drillAnimation.running && drillAnimation.pendingParentId === parentId)
            return;

        drillAnimation.pendingParentId = parentId;
        drillAnimation.restart();
    }

    function drillOut() {
        if (root.currentParentId === "")
            return;

        if (drillAnimation.running && drillAnimation.pendingParentId === "")
            return;

        drillAnimation.pendingParentId = "";
        drillAnimation.restart();
    }

    function toggleCategory(id) {
        // Clone target is `Object.create(null)` to preserve the
        // prototype-less invariant on the property (see
        // expandedCategories docstring above).
        const next = Object.assign(Object.create(null), root.expandedCategories);
        next[id] = !root._isExpanded(id);
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
        // NOTE: every row dict here uses `pageId` (not `id`) for the
        // model-role name. The delegate (SidebarRow.qml) consumes it
        // as `required property string pageId` — `id` would shadow
        // the QML id: directive and trips up qmlformat's parser.
        if (root.searchText.length === 0) {
            const out = [];
            const walk = function walk(parentId, depth) {
                const kids = root._scopeChildren(parentId);
                for (let i = 0; i < kids.length; ++i) {
                    const child = kids[i];
                    const childHasChildren = root._hasChildren(child.id);
                    const collapsible = child.isCollapsible === true && childHasChildren;
                    out.push({
                        "pageId": child.id,
                        "title": child.title,
                        "iconSource": child.iconSource,
                        "hasQmlSource": child.hasQmlSource === true,
                        "_depth": depth,
                        "_isCollapsibleHeader": collapsible,
                        "_isDrillParent": !collapsible && childHasChildren,
                        "_isExpanded": collapsible && root._isExpanded(child.id),
                        "_isDivider": false
                    });
                    if (collapsible && root._isExpanded(child.id))
                        walk(child.id, depth + 1);

                    // Section divider — synthetic row pushed immediately
                    // after an entry that requested `hasDividerAfter`.
                    // Suppressed in search mode because dividers carry
                    // no match metadata and would break the flat result
                    // list's reading order. The pageId includes parentId +
                    // child.id so it stays unique even with identical
                    // labels under different parents.
                    if (child.hasDividerAfter === true)
                        out.push({
                            "pageId": "__divider__" + parentId + "/" + child.id,
                            "title": "",
                            "iconSource": "",
                            "hasQmlSource": false,
                            "_depth": depth,
                            "_isCollapsibleHeader": false,
                            "_isDrillParent": false,
                            "_isExpanded": false,
                            "_isDivider": true
                        });
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
                const childBreadcrumb = breadcrumb.length === 0 ? child.title : breadcrumb + " / " + child.title;
                // Always recurse so descendants can match — but if
                // this parent is itself navigable (`hasQmlSource`),
                // also consider its own label as a match candidate.
                // The previous form skipped parents entirely, so a
                // navigable category page (one that has children AND
                // a qmlSource) was unreachable through search.
                if (grand)
                    collect(child.id, childBreadcrumb);

                if (!child.hasQmlSource)
                    continue;

                if (childBreadcrumb.toLowerCase().indexOf(needle) >= 0)
                    matches.push({
                        "pageId": child.id,
                        "title": childBreadcrumb,
                        "iconSource": child.iconSource,
                        "hasQmlSource": true,
                        "_depth": 0,
                        "_isCollapsibleHeader": false,
                        "_isDrillParent": false,
                        "_isExpanded": false,
                        "_isDivider": false
                    });
            }
        };
        collect(root.currentParentId, "");
        return matches;
    }

    // Deep-equal the 9 known role values between a model row and the
    // wanted item. Used to skip ListModel.set() when nothing actually
    // changed — set() replaces all roles AND fires their change
    // signals, which on a search-field keystroke meant every
    // delegate's required-property bindings re-fired even for rows
    // whose content was identical. Cheap JS compare beats animation
    // + repaint churn.
    function _itemEqualsRow(item, row) {
        return item.pageId === row.pageId && item.title === row.title && item.iconSource === row.iconSource && item.hasQmlSource === row.hasQmlSource && item._depth === row._depth && item._isCollapsibleHeader === row._isCollapsibleHeader && item._isDrillParent === row._isDrillParent && item._isExpanded === row._isExpanded && item._isDivider === row._isDivider;
    }

    function _refreshModel() {
        const wanted = root._visibleItems();
        const wantedIds = new Set(wanted.map(w => {
            return w.pageId;
        }));
        for (let i = visibleModel.count - 1; i >= 0; --i) {
            if (!wantedIds.has(visibleModel.get(i).pageId))
                visibleModel.remove(i);
        }
        for (let i = 0; i < wanted.length; ++i) {
            const item = wanted[i];
            if (i < visibleModel.count && visibleModel.get(i).pageId === item.pageId) {
                // Position correct + id matches — only re-set roles if
                // ANY of them actually changed. Otherwise the row's
                // delegate sees no required-property changes, no
                // bindings re-evaluate, no Behavior animations re-
                // arm. Major win on each search-field keystroke.
                if (!_itemEqualsRow(item, visibleModel.get(i)))
                    visibleModel.set(i, item);

                continue;
            }
            let currentIdx = -1;
            for (let j = i; j < visibleModel.count; ++j) {
                if (visibleModel.get(j).pageId === item.pageId) {
                    currentIdx = j;
                    break;
                }
            }
            if (currentIdx === -1) {
                visibleModel.insert(i, item);
            } else {
                visibleModel.move(currentIdx, i, 1);
                // Same compare after the move — set() only if the
                // post-move row differs from the wanted item.
                if (!_itemEqualsRow(item, visibleModel.get(i)))
                    visibleModel.set(i, item);
            }
        }
    }

    spacing: 0
    onCurrentParentIdChanged: _refreshModel()
    onExpandedCategoriesChanged: _refreshModel()
    onSearchTextChanged: _refreshModel()
    Component.onCompleted: {
        // Suppress per-row add Transitions for the initial fill so the
        // sidebar doesn't visibly accordion-expand every top-level row
        // on the very first paint. Same mechanism the drill/search
        // paths use; flip back on the next event-loop tick so steady-
        // state animations resume.
        root._suppressAccordion = true;
        _refreshModel();
        Qt.callLater(() => {
            root._suppressAccordion = false;
        });
    }

    Connections {
        function onCurrentPageIdChanged() {
            root._refreshModel();
        }

        target: root.controller
    }

    // Late-registered pages need to appear in the rail without a
    // restart. The registry's pageRegistered signal fires once per
    // registerPage() call; refreshing on each is cheap (the model
    // diff keeps the visible delegates stable) and covers async
    // catalog warm-up flows (plugin loading, dynamic registration).
    Connections {
        function onPageRegistered() {
            root._refreshModel();
        }

        target: root.controller.registry
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
        placeholderText: qsTr("Search...")
        // In compact mode there's no room for a search field — hide
        // and clear so a stale filter doesn't keep the rail filtered
        // when the user collapses the sidebar. Clearing via the alias
        // (root.searchText = "") instead of directly on `text` makes
        // the side effect visible to external consumers that might be
        // tracking the aliased property.
        visible: !root.compact
        onVisibleChanged: {
            if (!visible)
                root.searchText = "";
        }
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

            SidebarBackButton {
                id: backButton

                visible: root.currentParentId !== "" && root.searchText.length === 0
                backButtonHeight: root.backButtonHeight
                onBackClicked: root.drillOut()
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

                delegate: SidebarRow {
                    // `isCurrent` is computed here (not inside SidebarRow)
                    // because the controller reference and currentPageId
                    // binding chain live at this scope. SidebarRow stays
                    // controller-agnostic — it just paints whatever
                    // `isCurrent` resolves to.
                    isCurrent: !_isCollapsibleHeader && hasQmlSource && root.controller.currentPageId === pageId
                    compact: root.compact
                    navRowHeight: root.navRowHeight
                    accentBarWidth: root.accentBarWidth
                    trailingDelegate: root.trailingDelegate
                    onNavigationRequested: pid => root.controller.currentPageId = pid
                    onCategoryToggleRequested: pid => root.toggleCategory(pid)
                    onDrillIntoRequested: pid => root.drillInto(pid)
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
        onLoaded: PhosphorLoaderHelpers.bindItemWidthToLoader(footerLoader)
    }
}
