// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Disclosure header for a collapsible filter row.
 *
 * A chevron "Filter" toggle plus an accent dot shown while collapsed when a
 * filter is active (so a hidden filter stays discoverable). The consumer owns
 * the chip content and gates its visibility on `expanded`:
 *
 *   FilterDisclosureHeader { id: filterHeader; hasActiveFilters: ... }
 *   CollapsibleChipFlow { open: filterHeader.expanded; <chips> }
 */
RowLayout {
    id: root

    /// Whether the consumer's filter chips should be shown. Driven by the
    /// toggle button (the single source of truth) so the state can't desync;
    /// collapsed by default to keep the page uncluttered.
    readonly property alias expanded: toggleButton.checked
    /// When collapsed, show an accent dot if a filter is currently applied.
    property bool hasActiveFilters: false

    Layout.fillWidth: true
    spacing: Kirigami.Units.smallSpacing

    ToolButton {
        id: toggleButton

        text: i18nc("@action:button toggle the filter chips", "Filter")
        icon.name: toggleButton.checked ? "arrow-down" : "arrow-right"
        checkable: true
        Accessible.name: toggleButton.checked ? i18nc("@action:button", "Hide filters") : i18nc("@action:button", "Show filters")
    }

    Rectangle {
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: Kirigami.Units.smallSpacing * 1.5
        implicitHeight: implicitWidth
        radius: width / 2
        color: Kirigami.Theme.highlightColor
        visible: !root.expanded && root.hasActiveFilters
    }

    Item {
        Layout.fillWidth: true
    }
}
