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

    function updateHoverState() {
        var anyHovered = splitHorizontalButton.hovered || splitVerticalButton.hovered || fillButton.hovered || duplicateButton.hovered || deleteButton.hovered;
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

        buttonSize: actionButtons.buttonSize
        iconSource: "view-split-top-bottom"
        accessibleName: i18nc("@action:button", "Split horizontally")
        accessibleDescription: i18nc("@info:tooltip", "Split this zone into two horizontal sections")
        tooltipText: i18nc("@tooltip", "Split horizontally")
        onHoveredChanged: actionButtons.updateHoverState()
        onActivated: actionButtons.root.splitHorizontalRequested()
    }

    ZoneActionButton {
        id: splitVerticalButton

        buttonSize: actionButtons.buttonSize
        iconSource: "view-split-left-right"
        accessibleName: i18nc("@action:button", "Split vertically")
        accessibleDescription: i18nc("@info:tooltip", "Split this zone into two vertical sections")
        tooltipText: i18nc("@tooltip", "Split vertically")
        onHoveredChanged: actionButtons.updateHoverState()
        onActivated: actionButtons.root.splitVerticalRequested()
    }

    ZoneActionButton {
        id: fillButton

        buttonSize: actionButtons.buttonSize
        iconSource: "zoom-fit-best"
        accessibleName: i18nc("@action:button", "Fill available space")
        accessibleDescription: i18nc("@info:tooltip", "Expand zone to fill available empty space")
        tooltipText: i18nc("@tooltip", "Fill available space")
        onHoveredChanged: actionButtons.updateHoverState()
        onActivated: actionButtons.root.animatedExpandToFill()
    }

    ZoneActionButton {
        id: duplicateButton

        buttonSize: actionButtons.buttonSize
        iconSource: "edit-copy"
        accessibleName: i18nc("@action:button", "Duplicate zone")
        accessibleDescription: i18nc("@info:tooltip", "Create a copy of this zone")
        tooltipText: i18nc("@tooltip", "Duplicate zone")
        onHoveredChanged: actionButtons.updateHoverState()
        onActivated: actionButtons.root.duplicateRequested()
    }

    ZoneActionButton {
        id: deleteButton

        buttonSize: actionButtons.buttonSize
        iconSource: "edit-delete"
        accessibleName: i18nc("@action:button", "Delete zone")
        accessibleDescription: i18nc("@info:tooltip", "Remove this zone from the layout")
        tooltipText: i18nc("@tooltip", "Delete zone")
        useNegativeColor: true
        onHoveredChanged: actionButtons.updateHoverState()
        onActivated: actionButtons.root.deleteRequested()
    }

}
