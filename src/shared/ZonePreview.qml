// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Shared zone preview component for rendering layout zones
 *
 * Used by:
 * - KCM LayoutThumbnail for settings page previews
 * - ZoneSelectorWindow for drag-and-drop zone selection
 * - LayoutPreview for layout selection popup
 *
 * Renders zones with consistent styling, gaps, numbers, and theming.
 */
Item {
    // ═══════════════════════════════════════════════════════════════════════════
    // Required Properties
    // ═══════════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════════
    // Optional Properties
    // ═══════════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════════
    // Color Properties (with sensible defaults)
    // ═══════════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════════
    // Signals
    // ═══════════════════════════════════════════════════════════════════════════
    // ═══════════════════════════════════════════════════════════════════════════
    // Zone Rendering
    // ═══════════════════════════════════════════════════════════════════════════

    id: root

    /// Array of zone objects with relativeGeometry: { x, y, width, height }
    required property var zones
    /// Whether this preview is in "active/selected" state (affects coloring)
    property bool isActive: false
    /// Whether this preview is hovered
    property bool isHovered: false
    /// Index of the currently selected zone (-1 for none)
    property int selectedZoneIndex: -1
    /// Gap between zones in pixels
    property real zonePadding: 1
    /// Minimum zone size in pixels
    property int minZoneSize: 8
    /// Whether to show zone numbers
    property bool showZoneNumbers: true
    /// Animation duration in milliseconds
    property int animationDuration: 150
    /// Whether zone click/hover signals are enabled (disable for thumbnail use)
    property bool interactive: false
    /// Zone fill opacity when not active/hovered
    property real inactiveOpacity: 0.25
    /// Zone fill opacity when active/hovered
    property real activeOpacity: 0.45
    /// Border opacity
    property real borderOpacity: 0.5

    /// Emitted when a zone is clicked
    signal zoneClicked(int index)
    /// Emitted when a zone is hovered
    signal zoneHovered(int index)
    /// Emitted when hover leaves a zone
    signal zoneUnhovered(int index)

    Repeater {
        model: root.zones || []

        delegate: Rectangle {
            id: zoneRect

            required property var modelData
            required property int index
            // Parse relative geometry
            property var relGeo: modelData.relativeGeometry || {
            }
            property real relX: relGeo.x || 0
            property real relY: relGeo.y || 0
            property real relWidth: relGeo.width || 0.25
            property real relHeight: relGeo.height || 1
            // Check if this zone is selected
            property bool isZoneSelected: root.selectedZoneIndex === index

            // Position and size with gaps
            x: relX * root.width + root.zonePadding
            y: relY * root.height + root.zonePadding
            width: Math.max(root.minZoneSize, relWidth * root.width - root.zonePadding * 2)
            height: Math.max(root.minZoneSize, relHeight * root.height - root.zonePadding * 2)
            // Zone fill color
            color: {
                var baseColor = Kirigami.Theme.textColor;
                var opacity = (root.isActive || root.isHovered || isZoneSelected) ? root.activeOpacity : root.inactiveOpacity;
                return Qt.rgba(baseColor.r, baseColor.g, baseColor.b, opacity);
            }
            // Border
            border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.borderOpacity)
            border.width: isZoneSelected ? 2 : 1
            radius: Kirigami.Units.smallSpacing * 0.5

            // Zone number label
            Label {
                anchors.centerIn: parent
                // Use actual zoneNumber from data if available, otherwise fall back to index + 1
                text: modelData.zoneNumber !== undefined ? modelData.zoneNumber : (index + 1)
                font.pixelSize: Math.min(parent.width, parent.height) * 0.4
                font.bold: true
                color: Kirigami.Theme.textColor
                opacity: (root.isActive || root.isHovered) ? 0.8 : 0.5
                visible: root.showZoneNumbers && parent.width >= 16 && parent.height >= 16

                Behavior on opacity {
                    NumberAnimation {
                        duration: root.animationDuration
                    }

                }

            }

            // Optional: Mouse interaction for zone selection (only when interactive)
            MouseArea {
                anchors.fill: parent
                hoverEnabled: root.interactive
                enabled: root.interactive
                visible: root.interactive
                onEntered: root.zoneHovered(index)
                onExited: root.zoneUnhovered(index)
                onClicked: root.zoneClicked(index)
            }

            // Animations
            Behavior on color {
                ColorAnimation {
                    duration: root.animationDuration
                }

            }

            Behavior on border.width {
                NumberAnimation {
                    duration: root.animationDuration / 2
                }

            }

        }

    }

}
