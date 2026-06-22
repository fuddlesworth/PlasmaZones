// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.phosphor.control
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
 *     title (between label and drill chevron). Used by Phosphor for
 *     the snapping/tiling Switch + dirty badge.
 *   - `footerContent`: Component instantiated at the very bottom of
 *     the sidebar, OUTSIDE the scroll area. Stays visible across
 *     drill / scroll. Used by Phosphor for the persistent daemon
 *     status + enable/disable toggle.
 */
ColumnLayout {
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
    /** When false, the sticky in-sidebar search field is hidden (and its
     *  filter cleared). Lets a consumer that provides its own global search
     *  (e.g. a header command field) suppress the redundant sidebar search
     *  without losing the capability for other apps. */
    property bool searchEnabled: true
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
    readonly property real navRowHeight: Kirigami.Units.gridUnit * 2.5

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
        // Flip relative to the *displayed* state — _isExpanded()
        // treats an absent id as expanded (the rail starts open),
        // so the toggle must respect that view. A prior ternary
        // form mapped `undefined → true` (no-op for the user) and
        // required a second click to actually collapse a
        // never-toggled category.
        //
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

    // Defence in depth against a misregistered page graph that
    // names itself as its own ancestor (parent ↔ self cycle, or a
    // longer ring). Without these caps a search keystroke would
    // freeze the UI thread infinitely-recursing through the cycle.
    // Mirrors the same guard pattern Breadcrumbs.qml uses for the
    // current-page → root walk; the constant here is intentionally
    // larger (64 vs Breadcrumbs' 32) because the sidebar walk
    // descends a tree (depth scales with nesting) while Breadcrumbs
    // walks a single parent chain (length bounded by hierarchy
    // depth). The C++ side keeps Breadcrumbs in lockstep at 32 hops
    // (`kMaxParentChainHops`); the sidebar's value is QML-internal.
    readonly property int _maxWalkDepth: 64

    function _visibleItems() {
        // NOTE: every row dict here uses `pageId` (not `id`) for the
        // model-role name. The delegate (SidebarRow.qml) consumes it
        // as `required property string pageId` — `id` would shadow
        // the QML id: directive and trips up qmlformat's parser.
        //
        // CONSUMER ROLE CONTRACT
        // ──────────────────────
        // Roles emitted here are the public model schema the trailing
        // delegate consumes via its `modelData.<role>` reads. Naming
        // conventions:
        //   - `pageId`, `title`, `iconSource`, `hasQmlSource` — plain
        //     camelCase, the page identity surface.
        //   - `_depth`, `_isCollapsibleHeader`, `_isDrillParent`,
        //     `_isExpanded`, `_isDivider` — underscore-prefixed
        //     LAYOUT/STATE hints. The prefix signals "treat as
        //     view-internal detail" (don't store, don't compute
        //     derived values from), but they're part of the public
        //     model contract because the consumer's trailingDelegate
        //     must read them to render badges / active stripes
        //     correctly. Renaming any of these is a BREAKING change
        //     for downstream consumers (Phosphor Main.qml etc.).
        if (root.searchText.length === 0) {
            const out = [];
            // Flat seen-set tracks every visited page id (parent AND
            // child) so a misregistered tree where the same id appears
            // as a sibling under multiple parents — or as both parent
            // AND child of itself one level apart — can't drive
            // infinite recursion. A parent-id-only guard would miss
            // sibling-level duplicates because each parent walk
            // starts with its own seen-marker for itself only.
            const seen = new Set();
            const walk = function walk(parentId, depth, kids) {
                if (depth > root._maxWalkDepth || seen.has(parentId))
                    return;
                seen.add(parentId);
                for (let i = 0; i < kids.length; ++i) {
                    const child = kids[i];
                    // Skip duplicate child ids — a malformed registry
                    // can list the same page twice under one parent,
                    // or under multiple parents that both appear in
                    // the current scope; without this guard the
                    // delegate's required-property bindings see two
                    // rows with identical pageIds and ListView's
                    // diff against visibleModel goes sideways.
                    if (seen.has(child.id))
                        continue;

                    // Single _scopeChildren call per child — used for
                    // the hasChildren predicate AND (when expanded)
                    // the recursion. Two `childPagesData(child.id)`
                    // hits per render row was wasted work.
                    const childKids = root._scopeChildren(child.id);
                    const childHasChildren = childKids.length > 0;
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
                        walk(child.id, depth + 1, childKids);

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
            walk(root.currentParentId, 0, root._scopeChildren(root.currentParentId));
            return out;
        }
        const needle = root.searchText.toLowerCase();
        const matches = [];
        // Same flat seen-set semantics as the no-search branch above:
        // catches sibling-level dupes and self-referencing children
        // that a parent-id-only guard would miss.
        const seen = new Set();
        // Recursively walk down from a parent until a navigable
        // (hasQmlSource) descendant is found — used to route a
        // category-title search match to its first reachable leaf so
        // category-only parents (no qmlSource of their own) are not
        // unreachable through search.
        const findFirstNavigable = function findFirstNavigable(parentId, depth) {
            if (depth > root._maxWalkDepth)
                return null;
            const kids = root._scopeChildren(parentId);
            for (let i = 0; i < kids.length; ++i) {
                const child = kids[i];
                if (child.hasQmlSource)
                    return child;
                if (root._hasChildren(child.id)) {
                    const desc = findFirstNavigable(child.id, depth + 1);
                    if (desc)
                        return desc;
                }
            }
            return null;
        };
        const collect = function collect(parentId, breadcrumb, depth) {
            if (depth > root._maxWalkDepth || seen.has(parentId))
                return;
            seen.add(parentId);
            const kids = root._scopeChildren(parentId);
            for (let i = 0; i < kids.length; ++i) {
                const child = kids[i];
                // Sibling-level / self-loop dupe guard (mirrors the
                // no-search branch above).
                if (seen.has(child.id))
                    continue;

                // Fetch the child's children ONCE — _hasChildren below would
                // have called childPagesData(child.id) and the recursion into
                // collect() then re-calls _scopeChildren(child.id) on the next
                // iteration. Two registry hits per recursion step is wasted
                // work on a search-keystroke hot path. Mirrors the no-search
                // branch's `grandKids` optimisation.
                const grandKids = root._scopeChildren(child.id);
                const grand = grandKids.length > 0;
                const childBreadcrumb = breadcrumb.length === 0 ? child.title : breadcrumb + " / " + child.title;
                // Always recurse so descendants can match.
                if (grand)
                    collect(child.id, childBreadcrumb, depth + 1);

                const matchesNeedle = childBreadcrumb.toLowerCase().indexOf(needle) >= 0;
                if (child.hasQmlSource) {
                    if (matchesNeedle)
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
                } else if (matchesNeedle && grand) {
                    // Category-only parent (no qmlSource of its own)
                    // whose title matches — route the user to its
                    // first navigable descendant so they land
                    // somewhere useful. Falls through silently if
                    // the whole subtree is non-navigable.
                    const landing = findFirstNavigable(child.id, depth + 1);
                    if (landing) {
                        matches.push({
                            "pageId": landing.id,
                            "title": childBreadcrumb,
                            "iconSource": landing.iconSource,
                            "hasQmlSource": true,
                            "_depth": 0,
                            "_isCollapsibleHeader": false,
                            "_isDrillParent": false,
                            "_isExpanded": false,
                            "_isDivider": false
                        });
                    }
                }
            }
        };
        collect(root.currentParentId, "", 0);
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
        // on the very first paint. Held through the initial paint AND
        // any pages that register in the same startup batch — pages
        // registered asynchronously after the first paint are
        // legitimate user-visible additions and animate normally.
        root._suppressAccordion = true;
        _refreshModel();
        // Drop the suppression after a short delay (longer than a
        // single event-loop tick) so any plugin-loaded pages
        // registered during the initial QML evaluation batch also
        // land without animation. Steady-state additions trigger
        // through Connections.onPageRegistered below, after this
        // timer fires.
        suppressAccordionTimer.start();
    }

    Timer {
        id: suppressAccordionTimer
        interval: Kirigami.Units.shortDuration
        repeat: false
        onTriggered: root._suppressAccordion = false
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

        // Suppress-flag invariant is held by onStarted / onStopped
        // rather than ScriptAction bookends inside the sequence. The
        // sequence ScriptActions only fired when the animation ran
        // its sequence to completion — if `complete()` were called
        // externally (e.g. a stop+restart on a rapid double-drill,
        // or a window-close mid-animation), the trailing ScriptAction
        // never ran and `_suppressAccordion` stayed true forever,
        // freezing all subsequent accordion / row Transitions. The
        // animation's lifecycle signals fire on EVERY start/stop
        // path (including stop(), complete(), restart()), so the
        // invariant holds across all paths.
        onStarted: root._suppressAccordion = true
        onStopped: root._suppressAccordion = false

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
        visible: !root.compact && root.searchEnabled
        onVisibleChanged: {
            if (!visible)
                root.searchText = "";
        }
    }

    // Divides the search field from the list — only meaningful when the
    // search field is shown. Hidden (e.g. a consumer that disabled the
    // in-sidebar search, or compact mode) it would leave an orphaned top border.
    Kirigami.Separator {
        Layout.fillWidth: true
        visible: searchField.visible
    }

    // ── Scrollable list area ────────────────────────────────────────
    QQC2.ScrollView {
        id: listScroll

        Layout.fillWidth: true
        Layout.fillHeight: true
        // Inset the row list horizontally so the active-row accent
        // stripe and hover backgrounds don't run flush against the
        // window's left edge. Matches the SearchField's smallSpacing
        // inset above so rows align with the search field's left edge,
        // and balances the right-hand scrollbar gutter the ScrollView
        // already reserves (which is the "right padding" that was
        // present without a matching left inset).
        Layout.leftMargin: Kirigami.Units.smallSpacing
        Layout.rightMargin: Kirigami.Units.smallSpacing
        clip: true

        ColumnLayout {
            id: listColumn

            width: listScroll.availableWidth
            spacing: 0

            SidebarBackButton {
                id: backButton

                visible: root.currentParentId !== "" && root.searchText.length === 0
                backButtonHeight: root.backButtonHeight
                compact: root.compact
                // Show the parent category name (e.g. "‹ Snapping"); pageData()
                // returns an empty map for an unknown/empty id, so guard to "".
                title: root.currentParentId !== "" ? (root.controller.registry.pageData(root.currentParentId).title || "") : ""
                onBackClicked: root.drillOut()
            }

            // Drill-out rule — a first-class sibling (not buried in the back
            // row's background) so it shares the section dividers' largeSpacing
            // inset and lines up with the rows below.
            Kirigami.Separator {
                Layout.fillWidth: true
                Layout.leftMargin: root.compact ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing
                Layout.rightMargin: root.compact ? Kirigami.Units.smallSpacing : Kirigami.Units.largeSpacing
                visible: backButton.visible
            }

            ListView {
                id: listView

                Layout.fillWidth: true
                Layout.preferredHeight: contentHeight
                model: visibleModel
                interactive: false
                spacing: 0
                // Contain the accordion add/displaced transitions: without it
                // the in-flight rows (animating `y` as a category expands)
                // paint outside the list's bounds and are seen sliding down
                // behind the rows / footer below it.
                clip: true

                add: Transition {
                    enabled: !root._suppressAccordion

                    // Fade newly-revealed rows in AT their final position — no
                    // `y` animation. Translating added rows made the category's
                    // children visibly fly in from above the header; the
                    // `displaced` transition below already slides the rows after
                    // the insertion point down to open the gap, which is the
                    // accordion motion we actually want.
                    PhosphorMotionAnimation {
                        properties: "opacity"
                        from: 0
                        to: 1
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
                    id: rowItem

                    // `isCurrent` is computed here (not inside SidebarRow)
                    // because the controller reference and currentPageId
                    // binding chain live at this scope. SidebarRow stays
                    // controller-agnostic — it just paints whatever
                    // `isCurrent` resolves to. The required properties
                    // declared in SidebarRow (pageId, _isCollapsibleHeader,
                    // hasQmlSource) are addressed via the explicit `rowItem.`
                    // qualifier — qmllint warns on the bare-identifier form
                    // and a future Qt minor may tighten the rule.
                    isCurrent: !rowItem._isCollapsibleHeader && rowItem.hasQmlSource && root.controller.currentPageId === rowItem.pageId
                    compact: root.compact
                    navRowHeight: root.navRowHeight
                    trailingDelegate: root.trailingDelegate
                    onNavigationRequested: pid => {
                        // Synthetic divider rows have pageIds like
                        // "__divider__<parent>/<id>" — never route those
                        // into the controller. The SidebarRow click handler
                        // already guards via `_isDivider`, but the signal
                        // itself stays open to programmatic invocation;
                        // defending here keeps the contract honest.
                        if (!pid.startsWith("__divider__"))
                            root.controller.currentPageId = pid;
                    }
                    onCategoryToggleRequested: pid => root.toggleCategory(pid)
                    onDrillIntoRequested: pid => root.drillInto(pid)
                }
            }
        }
    }

    // ── Sticky footer slot (e.g. daemon status / enable toggle) ─────
    Loader {
        id: footerLoader

        // True iff the loaded item declares a `compact` property —
        // signals that the consumer Component is compact-aware. Unaware
        // consumers (no `compact` property) get the default suppression
        // behaviour (`visible: false` in compact mode); aware consumers
        // stay visible and receive the live `compact` value through
        // the injectIfAssignable below. Read with `hasOwnProperty` so
        // a runtime `undefined` value (consumer declared but didn't
        // initialise) still counts as compact-aware.
        readonly property bool _consumerIsCompactAware: item ? item.hasOwnProperty("compact") : false

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
        // Compact-mode policy: hide unaware consumers (most consumer
        // slots are wide pill surfaces that don't fit a 3-gridUnit
        // rail), but stay visible for compact-aware consumers — they
        // opt in by declaring a `property bool compact: false` on
        // their Component's root, and we feed `root.compact` into it
        // (onLoaded + the Connections block below). Mirrors
        // SidebarRow's compact-suppression pattern.
        visible: active && (!root.compact || _consumerIsCompactAware)
        sourceComponent: root.footerContent
        onLoaded: {
            PhosphorLoaderHelpers.bindItemWidthToLoader(footerLoader);
            // Opt-in compact awareness: assign root.compact onto the
            // loaded item only when the consumer declared the
            // property. Avoids polluting unaware consumers and
            // silently fails for ones that don't care.
            PhosphorLoaderHelpers.injectIfAssignable(footerLoader.item, "compact", root.compact);
        }

        // Keep the loaded item's compact property in sync with the
        // sidebar's compact state — the onLoaded handler runs once,
        // but compact can toggle live (window resize crosses the
        // 50-gridUnit threshold). Same injectIfAssignable contract:
        // no-op for unaware consumers.
        Connections {
            function onCompactChanged() {
                if (footerLoader.item)
                    PhosphorLoaderHelpers.injectIfAssignable(footerLoader.item, "compact", root.compact);
            }

            target: root
        }
    }
}
