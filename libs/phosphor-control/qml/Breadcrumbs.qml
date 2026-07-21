// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.control

/**
 * Breadcrumb trail for the current page.
 *
 * Walks parentId links upward from the current page; each segment but
 * the last is clickable and navigates to that ancestor.
 *
 * Visual model matches the legacy Phosphor chrome: plain Labels at
 * 0.5 opacity by default, fading to 0.8 + an underline on hover for
 * clickable segments. Separator is a `›` (U+203A) Label between
 * segments, not an icon — keeps the trail lightweight and theme-
 * independent.
 */
RowLayout {
    id: root

    required property ApplicationController controller
    /** Per-segment maximum width budget. Long localised crumbs
     *  (Spanish / German with deep nesting) would otherwise overflow the
     *  breadcrumb bar — clamp each crumb to this budget and elide with a
     *  middle ellipsis so both ends (parent context + leaf name) stay
     *  readable. Consumers can override for tighter or wider chrome. */
    property real maxSegmentWidth: Kirigami.Units.gridUnit * 20
    /** Mirror of Sidebar.flattenTree: when the rail renders the page tree
     *  as one flat list, a deep parent-chain crumb trail contradicts it
     *  (the user never navigated through those levels). In flat mode the
     *  trail is just the current page, using the same title override map
     *  the flat rail applies. */
    property bool flattenTree: false
    /** Flat-mode display-title overrides, page id → title (see
     *  Sidebar.flatTitleOverrides). Consulted only while flattenTree. */
    property var flatTitleOverrides: ({})
    //  Cycle guard EXTENDS ApplicationController::parentChainFor's
    //  kMaxParentChainHops with a seen-set: the C++ guard catches an
    //  N-hop cycle after 32 hops + warns; the QML guard breaks on
    //  first repeat. A misregistered page with `parentId == own id`
    //  (or two pages mutually parenting each other) would otherwise
    //  freeze the UI thread on first render.
    readonly property int _maxParentChainHops: 32
    // Bumped whenever the registry emits pageRegistered — gives the
    // `segments` binding below a dependency to track so a late-registered
    // page whose id matches `currentPageId` (or any ancestor on the
    // chain) refreshes the rendered crumb trail. Without this, the
    // binding only re-evaluates when `currentPageId` changes and a
    // post-registration ancestor title update would never reach QML.
    property int _registryTick: 0
    //* Ordered ancestors → current. Each entry is a page-data dict.
    readonly property var segments: {
        // Touch _registryTick so the binding declares its dependency on
        // post-registration updates. The value isn't read meaningfully
        // — it's the change-notify that matters.
        void root._registryTick;
        if (root.flattenTree) {
            const leaf = root.controller.registry.pageData(root.controller.currentPageId);
            if (!leaf || !leaf.id)
                return [];
            // Own-property guard — the override map is a plain object
            // literal, so an id colliding with an Object.prototype name
            // would read an inherited builtin as a truthy title (see the
            // prototype-less seen-map note below).
            if (Object.prototype.hasOwnProperty.call(root.flatTitleOverrides, leaf.id))
                return [Object.assign({}, leaf, {
                        "title": root.flatTitleOverrides[leaf.id]
                    })];
            return [leaf];
        }
        const out = [];
        // Object.create(null) gives a prototype-less map, so page ids
        // matching built-in property names ("constructor", "toString",
        // "hasOwnProperty") don't spuriously short-circuit on inherited
        // truthy values.
        const seen = Object.create(null);
        let id = root.controller.currentPageId;
        let hops = 0;
        while (id && hops < root._maxParentChainHops) {
            if (seen[id])
                break;

            seen[id] = true;
            const data = root.controller.registry.pageData(id);
            if (!data || !data.id)
                break;

            out.unshift(data);
            id = data.parentId;
            hops++;
        }
        return out;
    }

    spacing: Kirigami.Units.smallSpacing

    Connections {
        function onPageRegistered() {
            root._registryTick = root._registryTick + 1;
        }

        target: root.controller.registry
    }

    Repeater {
        model: root.segments

        delegate: RowLayout {
            id: segmentRow

            required property int index
            required property var modelData
            readonly property bool isLast: index === root.segments.length - 1
            // Where this crumb actually navigates. An ancestor crumb is
            // frequently a virtual category with no page of its own (registered
            // with an empty qmlSource); assigning its id to currentPageId
            // passes the registry's hasPage() check and then renders an empty
            // body, so route those to the category's first visible leaf
            // instead. Empty when the segment is the current page, or when a
            // category has no visible leaf under it in the current mode — those
            // stay inert rather than navigating into a blank viewport.
            readonly property string targetId: {
                if (isLast)
                    return "";
                if (modelData.hasQmlSource)
                    return modelData.id;
                return root.controller.registry.firstVisibleLeafId(modelData.id);
            }
            readonly property bool clickable: targetId.length > 0

            spacing: Kirigami.Units.smallSpacing

            // Wrap the label + mouse area in an Item that can take Tab
            // focus and respond to Space/Enter — Label alone isn't
            // focusable, so before this wrapper a screen reader / Tab
            // user could land on the breadcrumb (because Accessible.role:
            // Link declared it as actionable) but had no way to trigger
            // the navigation. activeFocusOnTab is gated on `clickable`
            // so the trailing (current-page) segment stays inert.
            Item {
                id: segmentItem

                // Activation helper shared by all three Keys handlers
                // below — Return / Enter / Space all do the same
                // "navigate to this segment" action. Without the
                // function the three handlers were identical 3-line
                // blocks and any future change had to be made thrice.
                function _activate() {
                    if (segmentRow.clickable)
                        root.controller.currentPageId = segmentRow.targetId;
                }

                // Long localised crumbs (e.g. "Mostrar configuración
                // avanzada de personalización") would otherwise overflow the
                // breadcrumb bar — clamp to the consumer-controllable
                // maxSegmentWidth budget and elide with a middle ellipsis so
                // both ends (parent context + leaf name) stay readable.
                Layout.preferredWidth: Math.min(segmentLabel.implicitWidth, root.maxSegmentWidth)
                Layout.preferredHeight: segmentLabel.implicitHeight
                activeFocusOnTab: segmentRow.clickable
                Accessible.name: segmentLabel.text
                Accessible.role: segmentRow.clickable ? Accessible.Link : Accessible.StaticText
                // Accept the event only for clickable segments —
                // the trailing (current-page) segment is inert, so
                // Return / Enter / Space should bubble up to the
                // parent (e.g. an outer Shortcut handler) rather
                // than being silently swallowed by a Link role that
                // has no activation to perform.
                Keys.onReturnPressed: function (event) {
                    event.accepted = segmentRow.clickable;
                    if (segmentRow.clickable)
                        segmentItem._activate();
                }
                Keys.onEnterPressed: function (event) {
                    event.accepted = segmentRow.clickable;
                    if (segmentRow.clickable)
                        segmentItem._activate();
                }
                Keys.onSpacePressed: function (event) {
                    event.accepted = segmentRow.clickable;
                    if (segmentRow.clickable)
                        segmentItem._activate();
                }

                QQC2.Label {
                    id: segmentLabel

                    anchors.fill: parent
                    // The wrapper Item supplies Accessible.name/role for the
                    // crumb; leaving this Label in the tree makes a screen
                    // reader announce each crumb twice.
                    Accessible.ignored: true
                    text: segmentRow.modelData.title
                    elide: Text.ElideMiddle
                    opacity: segmentRow.clickable && (segmentMouse.containsMouse || segmentItem.activeFocus) ? 0.8 : 0.5
                    font.underline: segmentRow.clickable && (segmentMouse.containsMouse || segmentItem.activeFocus)
                }

                MouseArea {
                    id: segmentMouse

                    anchors.fill: parent
                    hoverEnabled: segmentRow.clickable
                    enabled: segmentRow.clickable
                    cursorShape: segmentRow.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.controller.currentPageId = segmentRow.targetId
                }
            }

            QQC2.Label {
                visible: !segmentRow.isLast
                // U+203A SINGLE RIGHT-POINTING ANGLE QUOTATION MARK —
                // matches the legacy separator glyph. Lighter visual
                // weight than an icon and doesn't depend on the
                // freedesktop icon theme.
                text: "›"
                opacity: 0.5
                // Decorative: a screen reader should read the crumbs, not
                // the glyph between them.
                Accessible.ignored: true
            }
        }
    }

    // Spacer to push subsequent items in the parent layout to the right.
    Item {
        Layout.fillWidth: true
    }
}
