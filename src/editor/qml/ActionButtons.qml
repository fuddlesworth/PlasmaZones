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
    // Smaller icon size

    id: actionButtons

    required property var root // Parent zone component
    property bool anyButtonHovered: false
    // Button dimensions - smaller for better fit
    readonly property real buttonSize: Kirigami.Units.gridUnit * 2.5
    // 20px (reduced from 28px)
    readonly property real iconSize: Kirigami.Units.iconSizes.smallMedium
    // Calculate if buttons fit in zone
    // Required width: 5 buttons * buttonSize + 4 gaps * spacing + 2 margins
    readonly property real requiredWidth: 5 * buttonSize + 4 * spacing + 2 * anchors.margins
    readonly property real requiredHeight: buttonSize + 2 * anchors.margins
    // Check if buttons fit within zone dimensions
    readonly property bool buttonsFit: root.visualWidth >= requiredWidth && root.visualHeight >= requiredHeight

    anchors.top: parent.top
    anchors.right: parent.right
    anchors.margins: Kirigami.Units.smallSpacing // Use theme spacing
    spacing: Kirigami.Units.smallSpacing // Use theme spacing
    visible: (root.mouseOverZone || root.isSelected) && buttonsFit
    z: 100 // Well above everything else

    // Split horizontal button
    Rectangle {
        id: splitHorizontalButton

        width: actionButtons.buttonSize
        height: actionButtons.buttonSize
        radius: Kirigami.Units.smallSpacing // Use theme spacing
        color: splitHorizontalMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        z: 101

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "view-split-top-bottom"
            width: actionButtons.iconSize
            height: actionButtons.iconSize
        }

        MouseArea {
            id: splitHorizontalMouseArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            z: 102
            acceptedButtons: Qt.LeftButton
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button", "Split horizontally")
            Accessible.description: i18nc("@info:tooltip", "Split this zone into two horizontal sections")
            onEntered: {
                actionButtons.anyButtonHovered = true;
                actionButtons.root.anyButtonHovered = true;
            }
            onExited: {
                actionButtons.anyButtonHovered = splitVerticalMouseArea.containsMouse || fillMouseArea.containsMouse || duplicateMouseArea.containsMouse || deleteMouseArea.containsMouse;
                actionButtons.root.anyButtonHovered = actionButtons.anyButtonHovered;
            }
            onPressedChanged: {
                // Clear hover state when starting button action
                if (pressed) {
                    actionButtons.anyButtonHovered = true;
                    actionButtons.root.anyButtonHovered = true;
                }
            }
            onPressed: function(mouse) {
                mouse.accepted = true;
            }
            onClicked: function(mouse) {
                mouse.accepted = true;
                actionButtons.root.splitHorizontalRequested();
            }
        }

        ToolTip {
            visible: splitHorizontalMouseArea.containsMouse
            text: i18nc("@tooltip", "Split horizontally")
        }

    }

    // Split vertical button
    Rectangle {
        id: splitVerticalButton

        width: actionButtons.buttonSize
        height: actionButtons.buttonSize
        radius: Kirigami.Units.smallSpacing // Use theme spacing
        color: splitVerticalMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        z: 101

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "view-split-left-right"
            width: actionButtons.iconSize
            height: actionButtons.iconSize
        }

        MouseArea {
            id: splitVerticalMouseArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            z: 102
            acceptedButtons: Qt.LeftButton
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button", "Split vertically")
            Accessible.description: i18nc("@info:tooltip", "Split this zone into two vertical sections")
            onEntered: {
                actionButtons.anyButtonHovered = true;
                actionButtons.root.anyButtonHovered = true;
            }
            onExited: {
                actionButtons.anyButtonHovered = splitHorizontalMouseArea.containsMouse || fillMouseArea.containsMouse || duplicateMouseArea.containsMouse || deleteMouseArea.containsMouse;
                actionButtons.root.anyButtonHovered = actionButtons.anyButtonHovered;
            }
            onPressedChanged: {
                // Clear hover state when starting button action
                if (pressed) {
                    actionButtons.anyButtonHovered = true;
                    actionButtons.root.anyButtonHovered = true;
                }
            }
            onPressed: function(mouse) {
                mouse.accepted = true;
            }
            onClicked: function(mouse) {
                mouse.accepted = true;
                actionButtons.root.splitVerticalRequested();
            }
        }

        ToolTip {
            visible: splitVerticalMouseArea.containsMouse
            text: i18nc("@tooltip", "Split vertically")
        }

    }

    // Fill button
    Rectangle {
        id: fillButton

        width: actionButtons.buttonSize
        height: actionButtons.buttonSize
        radius: Kirigami.Units.smallSpacing // Use theme spacing
        color: fillMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        z: 101

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "zoom-fit-best"
            width: actionButtons.iconSize
            height: actionButtons.iconSize
        }

        MouseArea {
            id: fillMouseArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            z: 102
            acceptedButtons: Qt.LeftButton
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button", "Fill available space")
            Accessible.description: i18nc("@info:tooltip", "Expand zone to fill available empty space")
            onEntered: {
                actionButtons.anyButtonHovered = true;
                actionButtons.root.anyButtonHovered = true;
            }
            onExited: {
                actionButtons.anyButtonHovered = splitHorizontalMouseArea.containsMouse || splitVerticalMouseArea.containsMouse || duplicateMouseArea.containsMouse || deleteMouseArea.containsMouse;
                actionButtons.root.anyButtonHovered = actionButtons.anyButtonHovered;
            }
            onPressedChanged: {
                // Clear hover state when starting button action
                if (pressed) {
                    actionButtons.anyButtonHovered = true;
                    actionButtons.root.anyButtonHovered = true;
                }
            }
            onPressed: function(mouse) {
                mouse.accepted = true;
            }
            onClicked: function(mouse) {
                mouse.accepted = true;
                actionButtons.root.animatedExpandToFill();
            }
        }

        ToolTip {
            visible: fillMouseArea.containsMouse
            text: i18nc("@tooltip", "Fill available space")
        }

    }

    // Duplicate button
    Rectangle {
        id: duplicateButton

        width: actionButtons.buttonSize
        height: actionButtons.buttonSize
        radius: Kirigami.Units.smallSpacing // Use theme spacing
        color: duplicateMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        z: 101

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "edit-copy"
            width: actionButtons.iconSize
            height: actionButtons.iconSize
        }

        MouseArea {
            id: duplicateMouseArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            z: 102
            acceptedButtons: Qt.LeftButton
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button", "Duplicate zone")
            Accessible.description: i18nc("@info:tooltip", "Create a copy of this zone")
            onEntered: {
                actionButtons.anyButtonHovered = true;
                actionButtons.root.anyButtonHovered = true;
            }
            onExited: {
                actionButtons.anyButtonHovered = splitHorizontalMouseArea.containsMouse || splitVerticalMouseArea.containsMouse || fillMouseArea.containsMouse || deleteMouseArea.containsMouse;
                actionButtons.root.anyButtonHovered = actionButtons.anyButtonHovered;
            }
            onPressedChanged: {
                // Clear hover state when starting button action
                if (pressed) {
                    actionButtons.anyButtonHovered = true;
                    actionButtons.root.anyButtonHovered = true;
                }
            }
            onPressed: function(mouse) {
                mouse.accepted = true;
            }
            onClicked: function(mouse) {
                mouse.accepted = true;
                actionButtons.root.duplicateRequested();
            }
        }

        ToolTip {
            visible: duplicateMouseArea.containsMouse
            text: i18nc("@tooltip", "Duplicate zone")
        }

    }

    // Delete button
    Rectangle {
        id: deleteButton

        width: actionButtons.buttonSize
        height: actionButtons.buttonSize
        radius: Kirigami.Units.smallSpacing // Use theme spacing
        color: deleteMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.5) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        z: 101

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "edit-delete"
            width: actionButtons.iconSize
            height: actionButtons.iconSize
        }

        MouseArea {
            id: deleteMouseArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            propagateComposedEvents: false
            z: 102
            acceptedButtons: Qt.LeftButton
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button", "Delete zone")
            Accessible.description: i18nc("@info:tooltip", "Remove this zone from the layout")
            onEntered: {
                actionButtons.anyButtonHovered = true;
                actionButtons.root.anyButtonHovered = true;
            }
            onExited: {
                actionButtons.anyButtonHovered = splitHorizontalMouseArea.containsMouse || splitVerticalMouseArea.containsMouse || fillMouseArea.containsMouse || duplicateMouseArea.containsMouse;
                actionButtons.root.anyButtonHovered = actionButtons.anyButtonHovered;
            }
            onPressedChanged: {
                // Clear hover state when starting button action
                if (pressed) {
                    actionButtons.anyButtonHovered = true;
                    actionButtons.root.anyButtonHovered = true;
                }
            }
            onPressed: function(mouse) {
                mouse.accepted = true;
            }
            onClicked: function(mouse) {
                mouse.accepted = true;
                actionButtons.root.deleteRequested();
            }
        }

        ToolTip {
            visible: deleteMouseArea.containsMouse
            text: i18nc("@tooltip", "Delete zone")
        }

    }

}
