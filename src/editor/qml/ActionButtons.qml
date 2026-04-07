// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Action buttons component for zone operations
 *
 * Provides duplicate, delete, split, and fill buttons that appear on hover.
 * Positioned in the top-right corner of the zone.
 */
Row {
    id: actionButtons

    required property var root // Parent zone component
    property bool anyButtonHovered: false
    // Button dimensions
    readonly property real buttonSize: Kirigami.Units.gridUnit * 2.5
    readonly property real iconSize: Kirigami.Units.iconSizes.smallMedium
    // Calculate if buttons fit in zone
    readonly property real requiredWidth: 5 * buttonSize + 4 * spacing + 2 * anchors.margins
    readonly property real requiredHeight: buttonSize + 2 * anchors.margins
    readonly property bool buttonsFit: root.visualWidth >= requiredWidth && root.visualHeight >= requiredHeight

    function updateHoverState(hoveredArea) {
        var anyHovered = splitHorizontalButton.containsMouse || splitVerticalButton.containsMouse || fillButton.containsMouse || duplicateButton.containsMouse || deleteButton.containsMouse;
        actionButtons.anyButtonHovered = anyHovered;
        actionButtons.root.anyButtonHovered = anyHovered;
    }

    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: Kirigami.Units.smallSpacing
    spacing: Kirigami.Units.smallSpacing
    visible: (root.mouseOverZone || root.isSelected) && buttonsFit
    z: 100

    ZoneActionButton {
        id: splitHorizontalButton

        iconSource: "view-split-top-bottom"
        accessibleName: i18nc("@action:button", "Split horizontally")
        accessibleDescription: i18nc("@info:tooltip", "Split this zone into two horizontal sections")
        tooltipText: i18nc("@tooltip", "Split horizontally")
        onContainsMouseChanged: actionButtons.updateHoverState(this)
        onActivated: actionButtons.root.splitHorizontalRequested()
    }

    ZoneActionButton {
        id: splitVerticalButton

        iconSource: "view-split-left-right"
        accessibleName: i18nc("@action:button", "Split vertically")
        accessibleDescription: i18nc("@info:tooltip", "Split this zone into two vertical sections")
        tooltipText: i18nc("@tooltip", "Split vertically")
        onContainsMouseChanged: actionButtons.updateHoverState(this)
        onActivated: actionButtons.root.splitVerticalRequested()
    }

    ZoneActionButton {
        id: fillButton

        iconSource: "zoom-fit-best"
        accessibleName: i18nc("@action:button", "Fill available space")
        accessibleDescription: i18nc("@info:tooltip", "Expand zone to fill available empty space")
        tooltipText: i18nc("@tooltip", "Fill available space")
        onContainsMouseChanged: actionButtons.updateHoverState(this)
        onActivated: actionButtons.root.animatedExpandToFill()
    }

    ZoneActionButton {
        id: duplicateButton

        iconSource: "edit-copy"
        accessibleName: i18nc("@action:button", "Duplicate zone")
        accessibleDescription: i18nc("@info:tooltip", "Create a copy of this zone")
        tooltipText: i18nc("@tooltip", "Duplicate zone")
        onContainsMouseChanged: actionButtons.updateHoverState(this)
        onActivated: actionButtons.root.duplicateRequested()
    }

    ZoneActionButton {
        id: deleteButton

        iconSource: "edit-delete"
        accessibleName: i18nc("@action:button", "Delete zone")
        accessibleDescription: i18nc("@info:tooltip", "Remove this zone from the layout")
        tooltipText: i18nc("@tooltip", "Delete zone")
        useNegativeColor: true
        onContainsMouseChanged: actionButtons.updateHoverState(this)
        onActivated: actionButtons.root.deleteRequested()
    }

}
