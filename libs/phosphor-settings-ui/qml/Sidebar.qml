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
        placeholderText: qsTr("Search...")
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
                    // metrics. `highlighted` is intentionally NOT used
                    // because we paint the background ourselves below.
                    implicitHeight: Kirigami.Units.gridUnit * 2.2
                    leftPadding: Kirigami.Units.smallSpacing + (_depth * Kirigami.Units.gridUnit)
                    rightPadding: Kirigami.Units.smallSpacing
                    onClicked: {
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

                        Behavior on color {
                            PhosphorMotionAnimation {
                                profile: "widget.tint.fast"
                            }

                        }

                    }

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.smallSpacing

                        // Per-row icon. Collapsible-category headers
                        // don't have their own icon (the rotating
                        // chevron at the end of the row does that
                        // duty) — they show no leading icon in legacy.
                        Kirigami.Icon {
                            visible: !itemDelegate._isCollapsibleHeader && itemDelegate.iconSource !== ""
                            source: itemDelegate.iconSource
                            Layout.preferredWidth: Kirigami.Units.iconSizes.small
                            Layout.preferredHeight: Kirigami.Units.iconSizes.small
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
                            active: root.trailingDelegate !== null
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Right chevron — single icon, rotated 90°
                        // when an inline category is expanded. Shown
                        // for drill-parents AND collapsible category
                        // headers; not shown for leaves. Reduced
                        // opacity so it reads as ornament, not action.
                        Kirigami.Icon {
                            visible: itemDelegate._isDrillParent || itemDelegate._isCollapsibleHeader
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
