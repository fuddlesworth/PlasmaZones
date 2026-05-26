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
        // Short-circuit on either the already-displayed scope OR a
        // drill already in flight targeting the same parent — without
        // the second check, a double-click on a drill row would
        // restart the cross-fade mid-animation and the user would see
        // the panel briefly flash back to opacity 0.
        if (root.currentParentId === parentId)
            return ;

        if (drillAnimation.running && drillAnimation.pendingParentId === parentId)
            return ;

        drillAnimation.pendingParentId = parentId;
        drillAnimation.restart();
    }

    function drillOut() {
        if (root.currentParentId === "")
            return ;

        if (drillAnimation.running && drillAnimation.pendingParentId === "")
            return ;

        drillAnimation.pendingParentId = "";
        drillAnimation.restart();
    }

    function toggleCategory(id) {
        // Flip relative to the *displayed* state — _isExpanded() treats
        // an absent id as expanded (the rail starts open), so the
        // toggle must respect that view. The earlier ternary form
        // mapped undefined → true (no-op for the user) and required a
        // second click to actually collapse a never-toggled category.
        const next = Object.assign({
        }, root.expandedCategories);
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
                        "_isExpanded": collapsible && root._isExpanded(child.id),
                        "_isDivider": false
                    });
                    if (collapsible && root._isExpanded(child.id))
                        walk(child.id, depth + 1);

                    // Section divider — synthetic row pushed immediately
                    // after an entry that requested `hasDividerAfter`.
                    // Suppressed in search mode because dividers carry
                    // no match metadata and would break the flat result
                    // list's reading order. The id includes parentId +
                    // child.id so it stays unique even with identical
                    // labels under different parents.
                    if (child.hasDividerAfter === true)
                        out.push({
                        "id": "__divider__" + parentId + "/" + child.id,
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
                    "id": child.id,
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
        // when the user collapses the sidebar.
        visible: !root.compact
        onVisibleChanged: {
            if (!visible)
                text = "";

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

            QQC2.ItemDelegate {
                id: backButton

                visible: root.currentParentId !== "" && root.searchText.length === 0
                Layout.fillWidth: true
                // Match legacy row height for the back button (slightly
                // taller than nav rows so it reads as a header).
                implicitHeight: Kirigami.Units.gridUnit * 2.6
                leftPadding: Kirigami.Units.smallSpacing
                rightPadding: Kirigami.Units.smallSpacing
                Accessible.name: qsTr("Back")
                Accessible.role: Accessible.Button
                onClicked: root.drillOut()

                background: Rectangle {
                    color: backButton.hovered ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06) : "transparent"
                    radius: Kirigami.Units.smallSpacing

                    // Legacy back-button bottom separator — a 1-dp line
                    // tucked inside the row's bottom edge so the rail
                    // reads as "you're inside a sub-section".
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Kirigami.Units.smallSpacing
                        anchors.rightMargin: Kirigami.Units.smallSpacing
                        height: Math.round(Kirigami.Units.devicePixelRatio)
                        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.1)
                    }

                    Behavior on color {
                        PhosphorMotionAnimation {
                            profile: "widget.tint.fast"
                        }

                    }

                }

                contentItem: RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Icon {
                        source: "go-previous-symbolic"
                        Layout.preferredWidth: Kirigami.Units.iconSizes.small
                        Layout.preferredHeight: Kirigami.Units.iconSizes.small
                        opacity: 0.7
                    }

                    QQC2.Label {
                        Layout.fillWidth: true
                        text: qsTr("Back")
                        // Legacy back-button label uses demi-bold @
                        // 0.8 opacity — reads as a section header
                        // rather than another nav row.
                        font.weight: Font.DemiBold
                        opacity: 0.8
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
                    required property bool _isDivider
                    readonly property var entryData: ({
                        "id": id,
                        "title": title,
                        "iconSource": iconSource,
                        "hasQmlSource": hasQmlSource,
                        "_depth": _depth,
                        "_isCollapsibleHeader": _isCollapsibleHeader,
                        "_isDrillParent": _isDrillParent,
                        "_isExpanded": _isExpanded,
                        "_isDivider": _isDivider
                    })
                    // `isCurrent` drives the highlight background + left
                    // accent + label/icon font-weight/opacity. Only true
                    // for navigable leaves on the active page id; never
                    // true for collapsible-category headers (those
                    // expand/collapse rather than navigate) or drill-
                    // parents (those swap the sidebar scope).
                    readonly property bool isCurrent: !_isCollapsibleHeader && hasQmlSource && root.controller.currentPageId === id

                    width: ListView.view.width
                    // Legacy row height — explicit so the rail's
                    // vertical rhythm is stable regardless of label
                    // metrics. Dividers get a shorter slot than nav
                    // rows so the breaks read as breathing-room rather
                    // than empty rows. `highlighted` is intentionally
                    // NOT used because we paint the background
                    // ourselves below.
                    implicitHeight: _isDivider ? Kirigami.Units.largeSpacing : (Kirigami.Units.gridUnit * 2.2)
                    // Accessible.name is always the row's title — in
                    // compact mode the visible Label is hidden, so a
                    // screen reader would otherwise have no announce-
                    // able content. Divider rows are flagged so AT
                    // tools skip them rather than reading "Button".
                    Accessible.name: itemDelegate._isDivider ? "" : itemDelegate.title
                    Accessible.role: itemDelegate._isDivider ? Accessible.Separator : Accessible.Button
                    // Dividers are visual ornament — disable click
                    // routing and any focus/hover state so the cursor
                    // doesn't change passing over them.
                    enabled: !_isDivider
                    hoverEnabled: !_isDivider
                    // Compact mode collapses leftPadding to zero so the
                    // icon centers visually in the narrow rail. Depth
                    // indent is also dropped — there's no room to
                    // express hierarchy when only the icon is visible,
                    // and the user has the tooltip + active accent to
                    // orient by.
                    leftPadding: root.compact ? 0 : (Kirigami.Units.smallSpacing + (_depth * Kirigami.Units.gridUnit))
                    rightPadding: root.compact ? 0 : Kirigami.Units.smallSpacing
                    // Tooltip surfaces the row label when compact mode
                    // has hidden it. 300ms delay matches legacy. Held
                    // off for divider rows because they have no label.
                    // `QtQuick.Controls` is imported as QQC2 so the
                    // attached property has to be namespaced too —
                    // unqualified `ToolTip.x` reads as a non-existent
                    // attached object.
                    QQC2.ToolTip.visible: root.compact && itemDelegate.hovered && !itemDelegate._isDivider
                    QQC2.ToolTip.text: itemDelegate.title
                    QQC2.ToolTip.delay: 300
                    onClicked: {
                        if (_isDivider)
                            return ;

                        if (_isCollapsibleHeader)
                            root.toggleCategory(id);
                        else if (_isDrillParent)
                            root.drillInto(id);
                        else if (hasQmlSource)
                            root.controller.currentPageId = id;
                    }

                    background: Rectangle {
                        // Active row: highlight tinted at 12% — same
                        // tint legacy used so the visual weight matches
                        // KCM modules. Hover: 6% textColor for a
                        // subtle "interactive" cue. Both transitions
                        // run through `widget.tint.fast` so they feel
                        // snappy without flicker.
                        color: {
                            // Dividers paint nothing — the Separator
                            // child below provides their only visual.
                            if (itemDelegate._isDivider)
                                return "transparent";

                            if (itemDelegate.isCurrent)
                                return Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12);

                            if (itemDelegate.hovered)
                                return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.06);

                            return "transparent";
                        }
                        radius: Kirigami.Units.smallSpacing

                        // Left accent bar — 2.5dp wide, half the
                        // row's height, rounded ends, highlightColor.
                        // Only visible on the active leaf so the user's
                        // location reads at a glance even when the
                        // background tint is subtle.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.round(Kirigami.Units.devicePixelRatio * 2.5)
                            height: parent.height * 0.5
                            radius: width / 2
                            color: Kirigami.Theme.highlightColor
                            visible: itemDelegate.isCurrent
                        }

                        // Section divider line — only present on
                        // divider rows. Centered vertically with
                        // largeSpacing horizontal margins so the line
                        // visually clears row hover backgrounds above
                        // and below.
                        Kirigami.Separator {
                            visible: itemDelegate._isDivider
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Kirigami.Units.largeSpacing
                            anchors.rightMargin: Kirigami.Units.largeSpacing
                        }

                        Behavior on color {
                            PhosphorMotionAnimation {
                                profile: "widget.tint.fast"
                            }

                        }

                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing
                        // Divider rows hide all row content — the
                        // background's Kirigami.Separator carries the
                        // entire visual. Leaving the RowLayout present
                        // (and just zeroing its visibility) keeps the
                        // delegate's geometry stable.
                        visible: !itemDelegate._isDivider

                        // Per-row icon. Collapsible-category headers
                        // don't have their own icon (the rotating
                        // chevron at the end of the row does that
                        // duty) — they show no leading icon in legacy.
                        Kirigami.Icon {
                            visible: !itemDelegate._isCollapsibleHeader && itemDelegate.iconSource !== ""
                            source: itemDelegate.iconSource
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            // In compact mode the icon is the only
                            // visible row content; fillWidth lets the
                            // RowLayout center it in the rail. In
                            // normal mode it sits at its natural width
                            // with the label fillWidth'ing after it.
                            Layout.fillWidth: root.compact
                            Layout.alignment: root.compact ? Qt.AlignHCenter : Qt.AlignLeft
                            // Legacy opacity model: active rows go to
                            // 1.0, everything else sits at 0.7. The
                            // 120-ms `widget.hover` transition matches
                            // the label below so the row's accent
                            // changes feel synchronous.
                            opacity: itemDelegate.isCurrent ? 1 : 0.7

                            Behavior on opacity {
                                PhosphorMotionAnimation {
                                    profile: "widget.hover"
                                    durationOverride: 120
                                }

                            }

                        }

                        QQC2.Label {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 0
                            elide: Text.ElideRight
                            text: itemDelegate.title
                            // Compact mode hides the label entirely —
                            // the row reads as icon-only with a
                            // tooltip. Labels are still kept in the
                            // delegate so the screen reader's
                            // Accessible.name still gets a string.
                            visible: !root.compact
                            // Demi-bold on active rows AND collapsible
                            // category headers — both read as
                            // "anchored" surfaces. Other leaves get
                            // Normal so the active row pops.
                            font.weight: (itemDelegate.isCurrent || itemDelegate._isCollapsibleHeader) ? Font.DemiBold : Font.Normal
                            opacity: itemDelegate.isCurrent ? 1 : 0.7

                            Behavior on opacity {
                                PhosphorMotionAnimation {
                                    profile: "widget.hover"
                                    durationOverride: 120
                                }

                            }

                        }

                        Loader {
                            id: trailingLoader

                            property var modelData: itemDelegate.entryData

                            sourceComponent: root.trailingDelegate
                            // Trailing widgets (e.g. PlasmaZones'
                            // snapping/tiling Switch + dirty badge)
                            // are suppressed in compact mode — there's
                            // no horizontal room and the row reads as
                            // icon-only. Consumers that need a compact-
                            // mode trailing affordance can express it
                            // through the badge slot once that exists.
                            active: root.trailingDelegate !== null && !root.compact
                            visible: active
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Right chevron — single icon, rotated 90°
                        // when an inline category is expanded. Shown
                        // for drill-parents AND collapsible category
                        // headers; not shown for leaves or in compact
                        // mode (no room, and the active accent + drill
                        // change carries the affordance). Reduced
                        // opacity so it reads as ornament, not action.
                        Kirigami.Icon {
                            visible: (itemDelegate._isDrillParent || itemDelegate._isCollapsibleHeader) && !root.compact
                            source: "go-next"
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
                            Layout.alignment: Qt.AlignVCenter
                            opacity: 0.3
                            rotation: itemDelegate._isCollapsibleHeader && itemDelegate._isExpanded ? 90 : 0

                            Behavior on rotation {
                                PhosphorMotionAnimation {
                                    profile: "widget.hover"
                                    durationOverride: 150
                                }

                            }

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
