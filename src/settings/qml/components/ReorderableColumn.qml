// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import "../js/SearchAnchorHelpers.js" as SearchAnchors

/**
 * @brief Generic drag-reorderable, variable-height column of arbitrary rows.
 *
 * The mechanics extracted from RuleSectionList so any list with the same UX
 * model (a reorderable stack of expandable rows) can reuse them instead of
 * re-implementing the grip column + variable-height drop cascade. It owns:
 *   - the left-edge drag-handle strip (grip icon + drag MouseArea),
 *   - Alt+Up / Alt+Down keyboard reorder,
 *   - the per-row height publishing + prefix-sum offset cache that keeps the
 *     drop preview correct when rows differ in height (e.g. one is expanded).
 *
 * It is item-agnostic: the consumer supplies `items` (an array), tells the
 * container how to read a stable id and the reorderable flag off an item
 * (`idOf` / `reorderableOf`), provides the per-row content as `rowDelegate`,
 * and commits a move by handling `moveRequested(fromIndex, toIndex)`. The row
 * content reads its data from the Loader that hosts it: `parent.rowModelData`,
 * `parent.rowIndex`, `parent.rowReorderable`.
 *
 * RuleSectionList is the reference consumer (rules priority list); the
 * decoration ChainEditor is the second. See RuleSectionList for the original
 * per-comment rationale of each mechanic — the logic here is a faithful move.
 */
Item {
    id: root

    /// The row entries, in display order. Snapshotted here so the Repeater
    /// delegate's `modelData` doesn't collide with the array reference.
    required property var items
    /// Whether drag / keyboard reordering is offered at all. Per-item pinning
    /// still applies via `reorderableOf`.
    property bool reorderingEnabled: true
    /// Stable string id for an item — keys the height cache and the deep-link
    /// anchor so reorders don't invalidate them. Default reads `item.id`.
    property var idOf: function (item) {
        return (item && item.id !== undefined) ? item.id : "";
    }
    /// Whether a given item may be reordered (managed/pinned rows return false).
    property var reorderableOf: function (item) {
        return true;
    }
    /// Optional human-readable name resolver for assistive technology: given
    /// an item, returns the name announced for its focusable row. Falls back
    /// to `idOf` when unset — set it whenever the model carries a display
    /// name, so screen readers announce that instead of an opaque id.
    property var accessibleNameOf: null
    /// Optional deep-link anchor prefix ("rule:", …). Empty disables per-row
    /// anchor registration (the decoration list has no search deep-links).
    property string anchorPrefix: ""
    /// Per-row content. Its root reads `parent.rowModelData` / `parent.rowIndex`
    /// / `parent.rowReorderable` from the hosting Loader.
    default property Component rowDelegate

    /// Emitted on a committed drag drop or Alt+Up/Down. The consumer performs
    /// the actual reorder of its model (moveRule, chain-rewrite, …).
    signal moveRequested(int fromIndex, int toIndex)

    // Height of the header band the drag-handle column pins to (kept in the
    // header row rather than stretching through an expanded body), and the
    // fallback height for a freshly-instantiated delegate before its content has
    // reported its implicitHeight back. Set it at or below the consumer's
    // collapsed-row height so the grip never forces a gap under a short row.
    property real headerRowHeight: Kirigami.Units.gridUnit * 4

    // Drag state consumed by the per-delegate `visualOffset` cascade.
    property int dragFromIndex: -1
    property int dropTargetIndex: -1
    property bool isDragging: false

    // Per-id published height. Keyed by id (not index) so reorders don't
    // invalidate the map; reassigned whole on each publish so bindings re-run.
    property var delegateHeights: ({})

    // Prune cache entries whose id no longer appears in `items`, so the map
    // doesn't grow across deletions (every publish copies the whole map).
    // Rebuilding here rather than deleting in Component.onDestruction is
    // deliberate: the Repeater treats an `items` reassignment as a full model
    // reset (destroy all, recreate all), so a destruction-time delete would
    // also drop heights of rows that are merely being recreated, collapsing
    // expanded rows to the fallback height until they republish.
    onItemsChanged: {
        var next = {};
        var kept = 0;
        for (var i = 0; i < items.length; ++i) {
            var item = items[i];
            if (item === undefined || item === null)
                continue;
            var itemId = root.idOf(item);
            var h = delegateHeights[itemId];
            if (h !== undefined) {
                next[itemId] = h;
                kept++;
            }
        }
        if (kept !== Object.keys(delegateHeights).length)
            delegateHeights = next;
    }

    function setDelegateHeight(itemId, h) {
        if (!itemId || h <= 0 || delegateHeights[itemId] === h)
            return;
        var copy = Object.assign({}, delegateHeights);
        copy[itemId] = h;
        delegateHeights = copy;
    }

    function heightOf(idx) {
        if (idx < 0 || idx >= items.length)
            return headerRowHeight;
        var item = items[idx];
        if (item === undefined || item === null)
            return headerRowHeight;
        var h = delegateHeights[root.idOf(item)];
        return (h !== undefined && h > 0) ? h : headerRowHeight;
    }

    // Prefix-sum of row heights: `_offsets[i]` is the cumulative Y of row i.
    readonly property var _offsets: {
        var arr = [0];
        for (var i = 0; i < items.length; ++i)
            arr.push(arr[i] + heightOf(i));
        return arr;
    }

    function cumulativeY(idx) {
        return root._offsets[Math.max(0, Math.min(idx, items.length))] || 0;
    }

    function slotIndexAt(centerY) {
        for (var i = 0; i < items.length; ++i) {
            if (centerY < root._offsets[i + 1])
                return i;
        }
        return Math.max(0, items.length - 1);
    }

    readonly property real totalHeight: root._offsets[items.length] || 0
    readonly property real draggedHeight: dragFromIndex >= 0 ? heightOf(dragFromIndex) : headerRowHeight

    Layout.fillWidth: true
    Layout.preferredHeight: totalHeight
    implicitHeight: totalHeight
    clip: true

    Repeater {
        model: root.items

        delegate: Item {
            id: delegateRoot

            required property var modelData
            required property int index

            readonly property string _itemId: root.idOf(modelData)
            readonly property bool reorderable: root.reorderingEnabled && root.reorderableOf(modelData)

            readonly property real baseY: root.cumulativeY(index)
            readonly property real visualOffset: {
                if (!root.isDragging || index === root.dragFromIndex)
                    return 0;

                var from = root.dragFromIndex;
                var to = root.dropTargetIndex;
                if (from < 0 || to < 0)
                    return 0;

                if (from < to) {
                    if (index > from && index <= to)
                        return -root.draggedHeight;
                } else if (index >= to && index < from) {
                    return root.draggedHeight;
                }
                return 0;
            }

            readonly property real actualHeight: rowLayout.implicitHeight
            onActualHeightChanged: root.setDelegateHeight(delegateRoot._itemId, actualHeight)

            // The key this row registered under, captured at registration
            // time. `_itemId` is a live binding on `modelData`, and during a
            // model reset modelData detaches before Component.onDestruction
            // runs — it would re-evaluate to "" through idOf's null-safe
            // default and unregister the bare prefix instead of the real key.
            // unregisterSearchAnchor matches on item identity, so that wrong
            // key is a silent no-op and the real entry leaks, leaving the
            // page's anchor map pointing at a destroyed Item. Caching the id
            // makes the two halves symmetric by construction. Same shape as
            // LayoutGridDelegate and ProfileRow.
            property string _anchorId: ""

            Component.onCompleted: {
                root.setDelegateHeight(delegateRoot._itemId, actualHeight);
                if (root.anchorPrefix.length > 0) {
                    Qt.callLater(function () {
                        // The coalesced callback can fire after this row has
                        // been destroyed (model churn during fast filtering
                        // or reloads); the dying context resolves ids to
                        // undefined. Bail before dereferencing rather than
                        // throwing — same guard as the sibling delegates.
                        if (typeof delegateRoot === "undefined" || !delegateRoot || !delegateRoot.modelData)
                            return;
                        var pg = SearchAnchors.pageFor(delegateRoot);
                        if (pg) {
                            delegateRoot._anchorId = root.anchorPrefix + delegateRoot._itemId;
                            pg.registerSearchAnchor(delegateRoot._anchorId, delegateRoot);
                        }
                    });
                }
            }
            Component.onDestruction: {
                // Empty means registration never happened (no anchor prefix,
                // no hosting page, or the row died before the callLater ran),
                // so there is nothing to shed.
                if (delegateRoot._anchorId.length === 0)
                    return;
                var pg = SearchAnchors.pageFor(delegateRoot);
                if (pg)
                    pg.unregisterSearchAnchor(delegateRoot._anchorId, delegateRoot);
            }

            width: root.width
            height: actualHeight
            y: baseY + visualOffset
            z: dragArea.drag.active ? 100 : 0
            activeFocusOnTab: true
            Accessible.role: Accessible.ListItem
            Accessible.name: root.accessibleNameOf ? root.accessibleNameOf(modelData) : (root.idOf(modelData) || "")

            Keys.onPressed: event => {
                if (!(event.modifiers & Qt.AltModifier))
                    return;
                if (!delegateRoot.reorderable)
                    return;
                var snapshot = root.items;
                var from = delegateRoot.index;
                var to = from;
                if (event.key === Qt.Key_Up) {
                    event.accepted = true;
                    if (from <= 0)
                        return;
                    to = from - 1;
                } else if (event.key === Qt.Key_Down) {
                    event.accepted = true;
                    if (from >= snapshot.length - 1)
                        return;
                    to = from + 1;
                } else {
                    return;
                }
                root.moveRequested(from, to);
            }

            RowLayout {
                id: rowLayout

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: 0

                // Drag-handle column — scoped MouseArea so clicks on the row's
                // own buttons still reach them.
                Item {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium + Kirigami.Units.largeSpacing
                    Layout.preferredHeight: root.headerRowHeight

                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: Kirigami.Units.iconSizes.smallMedium
                        height: Kirigami.Units.iconSizes.smallMedium
                        source: "handle-sort"
                        visible: delegateRoot.reorderable
                        opacity: dragArea.containsMouse || dragArea.drag.active ? 0.7 : 0.3
                    }

                    // Pinned indicator for non-reorderable rows.
                    Kirigami.Icon {
                        anchors.centerIn: parent
                        width: Kirigami.Units.iconSizes.small
                        height: Kirigami.Units.iconSizes.small
                        source: "lock"
                        visible: !delegateRoot.reorderable && root.reorderingEnabled
                        opacity: 0.3
                    }

                    MouseArea {
                        id: dragArea

                        enabled: delegateRoot.reorderable
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                        drag.target: delegateRoot
                        drag.axis: Drag.YAxis
                        drag.minimumY: 0
                        drag.maximumY: Math.max(0, root.totalHeight - delegateRoot.actualHeight)

                        onPressed: {
                            root.dragFromIndex = delegateRoot.index;
                            root.dropTargetIndex = delegateRoot.index;
                            root.isDragging = true;
                        }
                        onReleased: {
                            var from = root.dragFromIndex;
                            var to = root.dropTargetIndex;
                            root.isDragging = false;
                            root.dragFromIndex = -1;
                            root.dropTargetIndex = -1;
                            // Snap the delegate back to its layout position
                            // before the consumer mutation reorders the model.
                            delegateRoot.y = Qt.binding(function () {
                                return delegateRoot.baseY + delegateRoot.visualOffset;
                            });
                            // BOTH indices are range-checked against the model
                            // as it stands now. The model can shrink mid-drag
                            // (an async consumer refresh), which leaves the
                            // index captured on press pointing past the end,
                            // and the consumer would then act on an
                            // out-of-range source.
                            if (from >= 0 && to >= 0 && from !== to && from < root.items.length && to < root.items.length)
                                root.moveRequested(from, to);
                        }
                        onPositionChanged: {
                            if (drag.active) {
                                var centerY = delegateRoot.y + delegateRoot.actualHeight / 2;
                                var targetIndex = root.slotIndexAt(centerY);
                                if (targetIndex !== root.dropTargetIndex)
                                    root.dropTargetIndex = targetIndex;
                            }
                        }
                    }
                }

                // Consumer-supplied row content. Reads its data off this Loader.
                Loader {
                    id: rowLoader

                    Layout.fillWidth: true

                    property var rowModelData: delegateRoot.modelData
                    property int rowIndex: delegateRoot.index
                    property bool rowReorderable: delegateRoot.reorderable

                    sourceComponent: root.rowDelegate
                }
            }

            Behavior on y {
                enabled: !dragArea.drag.active

                PhosphorMotionAnimation {
                    profile: "widget.reorder"
                    durationOverride: Kirigami.Units.longDuration
                }
            }
        }
    }
}
