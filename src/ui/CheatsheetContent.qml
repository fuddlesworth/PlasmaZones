// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Shortcut cheatsheet content — Item-rooted body hosted in
 * PassiveOverlayShell's cheatsheetSlot. Display-only: a centered card
 * listing every global shortcut grouped by category, filtered by the
 * tiling mode of the screen the sheet opened on.
 *
 * Data arrives via the host slot's bindings (C++ pushes `shortcuts`,
 * `currentMode`, `autotileAvailable` onto cheatsheetSlot; live mode
 * switches re-push and the group filter re-evaluates reactively).
 *
 * Keyboard: the shell surface is kbd-None, so Escape routes via the
 * daemon's dedicated ad-hoc grab (start.cpp); QML Shortcuts can never
 * fire here. The backdrop MouseArea is the pointer dismiss path.
 */
Item {
    id: root

    /// Catalog rows from ShortcutManager::cheatsheetModel(): one object per
    /// shortcut with id, label, category, categoryOrder, triggers (list of
    /// display strings), assigned (bool), mode
    /// ("all"|"snapping"|"autotile"|"scrolling").
    property var shortcuts: []
    /// Tiling mode of the screen the sheet opened on:
    /// "snapping" | "autotile" | "scrolling".
    property string currentMode: "snapping"
    /// Global autotile feature gate. When off, the Autotile group hides in
    /// every mode (the mode is unreachable, so its shortcuts are noise).
    property bool autotileAvailable: true
    property string fontFamily: ""
    property real fontSizeScale: 1

    /// Idempotency latch for `dismissRequested` — same contract as
    /// LayoutPickerContent's: rapid backdrop clicks during the fade-out
    /// window collapse into one dismiss per show cycle. No writer resets
    /// it; the Loader re-instantiates this component on every show.
    property bool _dismissed: false

    signal dismissRequested

    function _requestDismiss() {
        if (_dismissed)
            return;

        _dismissed = true;
        root.dismissRequested();
    }

    /// True when the given catalog row applies in the current mode.
    function rowVisible(row) {
        if (row.mode === "autotile")
            return root.autotileAvailable && root.currentMode === "autotile";
        if (row.mode === "snapping")
            return root.currentMode === "snapping";
        if (row.mode === "scrolling")
            return root.currentMode === "scrolling";
        return true;
    }

    /// Rows regrouped into [{name, rows}] preserving the model's category
    /// order, with mode-inapplicable rows dropped. Recomputes reactively on
    /// shortcuts / currentMode / autotileAvailable changes.
    readonly property var groups: {
        var byCat = [];
        // Object.create(null): a plain {} would let `in` walk the prototype
        // chain, so a category named "constructor" would collide with a
        // Function slot.
        var index = Object.create(null);
        for (var i = 0; i < root.shortcuts.length; i++) {
            var row = root.shortcuts[i];
            if (!root.rowVisible(row))
                continue;

            if (!(row.category in index)) {
                index[row.category] = byCat.length;
                byCat.push({
                    name: row.category,
                    rows: []
                });
            }
            byCat[index[row.category]].rows.push(row);
        }
        return byCat;
    }

    /// Total packing cost of all groups. A group costs its rows plus a
    /// fixed heading + inter-group gap allowance; row units are all the
    /// same height so counting rows is an honest proxy for pixels.
    readonly property int totalUnits: {
        var t = 0;
        for (var g = 0; g < root.groups.length; g++)
            t += root.groups[g].rows.length + 2;
        return t;
    }

    /// Groups flowed into `metrics.columns` buckets newspaper-style, in
    /// display order. A group that straddles a column boundary splits: its
    /// remaining rows continue at the top of the next column under a
    /// repeated "(continued)" heading, so one huge group (Scrolling) no
    /// longer forces its column to tower over the others. Each bucket entry
    /// is {name, rows, continued}. Splits never orphan fewer than
    /// `minChunk` rows on either side of the boundary.
    readonly property var columnBuckets: {
        var n = metrics.columns;
        var buckets = [];
        for (var c = 0; c < n; c++)
            buckets.push([]);
        if (root.groups.length === 0)
            return buckets;

        var minChunk = 2;
        // Every split repeats a heading, growing the true total by 2 units;
        // budget for the worst case (one split per boundary) up front so the
        // last column doesn't silently absorb the overhead.
        var target = Math.ceil((root.totalUnits + 2 * (n - 1)) / n);
        var col = 0;
        var used = 0;
        for (var g = 0; g < root.groups.length; g++) {
            var group = root.groups[g];
            var offset = 0;
            while (offset < group.rows.length) {
                var rowsLeft = group.rows.length - offset;
                var space = target - used - 2;
                var lastCol = col === n - 1;
                if (!lastCol && space < minChunk && used > 0) {
                    // Not even room for a heading plus a minimal chunk:
                    // close this column and reconsider from the next.
                    col++;
                    used = 0;
                    continue;
                }
                var take = lastCol ? rowsLeft : Math.min(rowsLeft, Math.max(minChunk, space));
                var remainder = rowsLeft - take;
                if (remainder > 0 && remainder < minChunk) {
                    // Shrink this chunk rather than orphan a sliver in the
                    // next column; if that would make this chunk a sliver
                    // too, keep the group whole here instead.
                    take = rowsLeft - minChunk >= minChunk ? rowsLeft - minChunk : rowsLeft;
                }
                buckets[col].push({
                    name: group.name,
                    rows: group.rows.slice(offset, offset + take),
                    continued: offset > 0
                });
                used += take + 2;
                offset += take;
                if (!lastCol && used >= target) {
                    col++;
                    used = 0;
                }
            }
        }
        return buckets;
    }

    // Metrics mirror LayoutPickerContent's card chrome exactly (paddingSide
    // side/bottom padding, title one paddingSide down) so the two popups
    // read as siblings.
    QtObject {
        id: metrics

        readonly property int paddingSide: Kirigami.Units.gridUnit
        // Preferred width, shrunk to the available screen width when even a
        // single column at the preferred size would push the card (content +
        // side padding) past the screen edge — narrow screens get a
        // narrower, still fully visible column instead of clipping.
        readonly property int columnWidth: Math.min(Kirigami.Units.gridUnit * 18, Math.max(Kirigami.Units.gridUnit * 6, Math.floor(root.width * 0.9) - paddingSide * 2))
        readonly property int columnSpacing: Kirigami.Units.gridUnit * 2
        readonly property int maxColumns: 3
        readonly property int columns: {
            var avail = root.width * 0.9 - paddingSide * 2;
            var fit = Math.floor((avail + columnSpacing) / (columnWidth + columnSpacing));
            // Bound by content volume, not group count: groups split across
            // column boundaries, so one long group can legitimately span
            // several columns. One column per started ~8 units of content
            // keeps short sheets from spreading into slivers (the packer's
            // min-chunk rule still guarantees no column is near-empty).
            var worthwhile = Math.max(1, Math.ceil(root.totalUnits / 8));
            return Math.max(1, Math.min(maxColumns, Math.min(fit, worthwhile)));
        }
        readonly property int contentWidth: columns * columnWidth + (columns - 1) * columnSpacing
        readonly property int maxContentHeight: Math.max(0, Math.round(root.height * 0.85) - paddingSide * 3 - titleLabel.height)
    }

    // Backdrop — click outside to dismiss, same bare click-only backdrop
    // as LayoutPickerContent (no scrim; popup surfaces don't dim the
    // desktop).
    MouseArea {
        anchors.fill: parent
        onClicked: root._requestDismiss()
        Accessible.name: i18n("Dismiss shortcut cheatsheet")
        Accessible.role: Accessible.Button
        Accessible.onPressAction: root._requestDismiss()
    }

    QFZCommon.PopupFrame {
        id: container

        anchors.centerIn: parent
        width: metrics.contentWidth + metrics.paddingSide * 2
        // top padding + title + gap below title + content + bottom padding —
        // same vertical rhythm as LayoutPickerContent.
        height: titleLabel.height + scroller.height + metrics.paddingSide * 3

        // No container Accessible.name: the title label below is the single
        // announcement, matching LayoutPickerContent's card.

        // Absorb clicks inside the card so they never reach the backdrop —
        // same sibling z-order contract as LayoutPickerContent.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
            onClicked: function (mouse) {
                mouse.accepted = true;
            }
        }

        // Title — shared popup-card typography (PopupCardTitle), anchored
        // exactly like the picker's "Choose Layout".
        PopupCardTitle {
            id: titleLabel

            anchors.top: parent.top
            anchors.topMargin: metrics.paddingSide
            anchors.horizontalCenter: parent.horizontalCenter
            text: i18n("Keyboard Shortcuts")
        }

        // Empty-state fallback: every catalog row mode-filtered out. The
        // General group is mode-independent so this is unreachable with the
        // shipped taxonomy, but a data-driven guarantee is not a structural
        // one — degrade to a legible line instead of a bare title.
        Label {
            id: emptyStateLabel

            anchors.top: titleLabel.bottom
            anchors.topMargin: metrics.paddingSide
            anchors.horizontalCenter: parent.horizontalCenter
            width: metrics.contentWidth
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            text: i18n("No shortcuts apply in the current mode.")
            color: Kirigami.Theme.disabledTextColor
            visible: root.groups.length === 0
        }

        Flickable {
            id: scroller

            anchors.top: titleLabel.bottom
            anchors.topMargin: metrics.paddingSide
            anchors.horizontalCenter: parent.horizontalCenter
            width: metrics.contentWidth
            height: root.groups.length === 0 ? emptyStateLabel.height : Math.min(bucketsRow.implicitHeight, metrics.maxContentHeight)
            // Empty state: the fallback label occupies this rect instead —
            // hide the (empty) scroller so exactly one item owns the slot.
            visible: root.groups.length > 0
            contentWidth: width
            contentHeight: bucketsRow.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: bucketsRow

                spacing: metrics.columnSpacing

                Repeater {
                    model: root.columnBuckets

                    delegate: Column {
                        id: bucketColumn

                        required property var modelData

                        width: metrics.columnWidth
                        spacing: Kirigami.Units.largeSpacing

                        Repeater {
                            model: bucketColumn.modelData

                            delegate: Column {
                                id: groupColumn

                                required property var modelData

                                width: metrics.columnWidth
                                spacing: Kirigami.Units.smallSpacing

                                // Plain styled Label rather than a
                                // Kirigami.Heading: sibling overlay titles
                                // all bind pixelSize on Labels, and a
                                // Heading's level-driven pointSize would
                                // fight an explicit pixelSize on the same
                                // font. Sized 1.1x the row labels (the
                                // level-4 heading factor), tracking the
                                // user's overlay font like rows and caps.
                                Label {
                                    text: groupColumn.modelData.continued ? i18nc("category heading for a section that continues from the previous column", "%1 (continued)", groupColumn.modelData.name) : groupColumn.modelData.name
                                    color: Kirigami.Theme.disabledTextColor
                                    // Constrain to the column and wrap: a
                                    // long translated category name grows a
                                    // second line instead of overflowing
                                    // into the neighbouring column.
                                    width: groupColumn.width
                                    wrapMode: Text.Wrap
                                    font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
                                    font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 1.1 * root.fontSizeScale)
                                }

                                Repeater {
                                    model: groupColumn.modelData.rows

                                    delegate: RowLayout {
                                        id: shortcutRow

                                        required property var modelData

                                        width: metrics.columnWidth
                                        spacing: Kirigami.Units.smallSpacing

                                        Accessible.role: Accessible.StaticText
                                        Accessible.name: shortcutRow.modelData.assigned ? i18nc("shortcut row: action, keys", "%1, %2", shortcutRow.modelData.label, shortcutRow.modelData.triggers[0]) : i18nc("shortcut row: action unassigned", "%1, unassigned", shortcutRow.modelData.label)
                                        Accessible.description: shortcutRow.modelData.description

                                        // Plain-prose explanation from the
                                        // catalog, on hover. Rows without one
                                        // (empty description) show no tooltip.
                                        HoverHandler {
                                            id: rowHover

                                            enabled: shortcutRow.modelData.description.length > 0
                                        }

                                        ToolTip.visible: rowHover.hovered
                                        ToolTip.text: shortcutRow.modelData.description
                                        ToolTip.delay: Kirigami.Units.toolTipDelay

                                        Label {
                                            text: shortcutRow.modelData.label
                                            // The row announces a composed
                                            // "action, keys" Accessible.name;
                                            // keep the visible children out of
                                            // the a11y tree so screen readers
                                            // don't announce them twice.
                                            Accessible.ignored: true
                                            // Wrap, never elide: the model ships
                                            // group-contextual short labels sized
                                            // to fit, and a pathological case
                                            // (translation, custom font) grows a
                                            // second line instead of losing text.
                                            wrapMode: Text.Wrap
                                            font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
                                            font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * root.fontSizeScale)
                                            Layout.fillWidth: true
                                        }

                                        // One chip per key token of the first
                                        // bound sequence. A trailing "+" means
                                        // the plus key itself is the final token.
                                        Row {
                                            spacing: Math.round(Kirigami.Units.smallSpacing / 2)
                                            visible: shortcutRow.modelData.assigned

                                            Repeater {
                                                model: {
                                                    if (!shortcutRow.modelData.assigned)
                                                        return [];

                                                    var seq = shortcutRow.modelData.triggers[0];
                                                    var parts = seq.split("+").filter(function (p) {
                                                        return p.length > 0;
                                                    });
                                                    if (seq.endsWith("+"))
                                                        parts.push("+");
                                                    return parts;
                                                }

                                                delegate: KeyChip {
                                                    required property string modelData

                                                    text: modelData
                                                    fontFamily: root.fontFamily
                                                    fontSizeScale: root.fontSizeScale
                                                }
                                            }
                                        }

                                        Label {
                                            text: i18n("Unassigned")
                                            color: Kirigami.Theme.disabledTextColor
                                            font.italic: true
                                            visible: !shortcutRow.modelData.assigned
                                            // Covered by the row's composed
                                            // "%1, unassigned" Accessible.name.
                                            Accessible.ignored: true
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
