// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable "Group by / Sort by / direction" toolbar row.
 *
 * The shared group/sort affordance behind every listing page (Layouts, Tiling
 * algorithms, Rules). Driven entirely by injected data:
 *
 *   - `groupModel` / `sortModel`: arrays of already-localized option labels.
 *   - `groupByIndex` / `sortByIndex` / `sortAscending`: the live selection,
 *     read AND written here. The host owns persistence — this component holds
 *     no Settings of its own — so each page can persist however it likes
 *     (per-view-mode for Layouts, a single category for Rules).
 *   - `sortItemAvailable`: optional per-sort-option enablement. A `false` entry
 *     greys that option, suppresses its hover highlight, shows
 *     `disabledSortTooltip`, and makes selecting it a no-op (the Layouts page
 *     uses this for "Priority", which needs an order set on the Priority page).
 *
 * Emits `changed()` on every user-driven edit. After the host imperatively
 * rewrites the index properties (e.g. loading persisted state), it must call
 * `syncFromState()` — the ComboBox `currentIndex` bindings are initial-only and
 * a user interaction breaks them, exactly as the prior inline LayoutFilterBar
 * combos behaved.
 */
RowLayout {
    id: root

    property var groupModel: []
    property var sortModel: []
    property int groupByIndex: 0
    property int sortByIndex: 0
    property bool sortAscending: true
    // Optional `[bool]` parallel to `sortModel`. A missing or `true` entry means
    // available; `false` greys + disables that sort option. Empty = all available.
    property var sortItemAvailable: []
    property string disabledSortTooltip: ""

    signal changed

    // Re-point the combos at the current index properties. Call after any
    // imperative write to groupByIndex / sortByIndex (the combo currentIndex
    // binding is initial-only and breaks on user interaction).
    function syncFromState() {
        groupCombo.currentIndex = root.groupByIndex;
        sortCombo.currentIndex = root.sortByIndex;
    }

    function _sortAvailable(index) {
        return !(root.sortItemAvailable.length > index && root.sortItemAvailable[index] === false);
    }

    spacing: Kirigami.Units.smallSpacing

    // ── Group By ──────────────────────────────────────────────────────────────
    Label {
        text: i18n("Group:")
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }

    ComboBox {
        id: groupCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
        model: root.groupModel
        // Initial-only binding — syncFromState() re-syncs after host writes.
        currentIndex: root.groupByIndex
        Accessible.name: i18n("Group by")
        onActivated: index => {
            if (index < 0 || index >= count)
                return;

            root.groupByIndex = index;
            root.changed();
        }
    }

    // ── Sort By ───────────────────────────────────────────────────────────────
    Label {
        text: i18n("Sort:")
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
    }

    ComboBox {
        id: sortCombo

        Layout.preferredWidth: Kirigami.Units.gridUnit * 8
        model: root.sortModel
        // Initial-only binding — syncFromState() re-syncs after host writes.
        currentIndex: root.sortByIndex
        Accessible.name: i18n("Sort by")
        onActivated: index => {
            if (index < 0 || index >= count)
                return;

            // Unavailable option — swallow the selection and revert the visible
            // value rather than applying a sort the host can't honour yet.
            if (!root._sortAvailable(index)) {
                currentIndex = root.sortByIndex;
                return;
            }
            root.sortByIndex = index;
            root.changed();
        }

        // Keep unavailable delegates ENABLED — a disabled delegate receives no
        // hover events, so the explanatory tooltip would never show. Grey it,
        // suppress the hover highlight, and let onActivated swallow the pick.
        delegate: ItemDelegate {
            required property int index
            required property var modelData

            readonly property bool unavailable: !root._sortAvailable(index)

            width: sortCombo.width
            text: modelData
            opacity: unavailable ? 0.5 : 1
            highlighted: !unavailable && sortCombo.highlightedIndex === index
            ToolTip.visible: hovered && unavailable && root.disabledSortTooltip.length > 0
            ToolTip.delay: Kirigami.Units.toolTipDelay
            ToolTip.text: root.disabledSortTooltip
        }
    }

    ToolButton {
        icon.name: root.sortAscending ? "view-sort-ascending" : "view-sort-descending"
        icon.width: Kirigami.Units.iconSizes.smallMedium
        icon.height: Kirigami.Units.iconSizes.smallMedium
        onClicked: {
            root.sortAscending = !root.sortAscending;
            root.changed();
        }
        Accessible.name: root.sortAscending ? i18n("Sort ascending") : i18n("Sort descending")
        ToolTip.visible: hovered
        ToolTip.text: root.sortAscending ? i18n("Ascending") : i18n("Descending")
    }
}
