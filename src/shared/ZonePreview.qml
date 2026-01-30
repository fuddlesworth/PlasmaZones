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
    /// Gap between zones in pixels (applied as zonePadding/2 per side between adjacent zones)
    property real zonePadding: 1
    /// Gap at screen edges in pixels
    property real edgeGap: 1
    /// Minimum zone size in pixels
    property int minZoneSize: 8
    /// Whether to show zone numbers
    property bool showZoneNumbers: true
    /// Animation duration in milliseconds
    property int animationDuration: 150
    /// Whether zone click/hover signals are enabled (disable for thumbnail use)
    property bool interactive: false
    /// Whether all zones highlight together (for autotile layouts)
    property bool highlightAllZones: false
    /// Zone fill opacity when not active/hovered
    property real inactiveOpacity: 0.25
    /// Zone fill opacity when active/hovered
    property real activeOpacity: 0.45
    /// Border opacity
    property real borderOpacity: 0.5
    /// Highlight color for selected zones (default: theme highlight)
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    /// Inactive color for non-selected zones (default: theme text)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    /// Border color (default: theme text)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    /// Scale factor when zone is hovered (1.0 = no scale, set > 1.0 to enable)
    property real hoverScale: 1.0

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
            property var relGeo: modelData.relativeGeometry || {}
            property real relX: relGeo.x || 0
            property real relY: relGeo.y || 0
            property real relWidth: relGeo.width || 0.25
            property real relHeight: relGeo.height || 1
            // Check if this zone is selected (or all zones if highlightAllZones)
            property bool isZoneSelected: root.highlightAllZones
                ? root.selectedZoneIndex >= 0
                : root.selectedZoneIndex === index
            // Track per-zone hover state
            property bool isZoneHovered: root.interactive && zoneMouseArea.containsMouse

            // Detect screen boundaries (tolerance 0.01)
            readonly property real edgeTolerance: 0.01
            readonly property real leftGap: relX < edgeTolerance ? root.edgeGap : root.zonePadding / 2
            readonly property real topGap: relY < edgeTolerance ? root.edgeGap : root.zonePadding / 2
            readonly property real rightGap: (relX + relWidth) > (1.0 - edgeTolerance) ? root.edgeGap : root.zonePadding / 2
            readonly property real bottomGap: (relY + relHeight) > (1.0 - edgeTolerance) ? root.edgeGap : root.zonePadding / 2

            // Position and size with differentiated gaps
            x: relX * root.width + leftGap
            y: relY * root.height + topGap
            width: Math.max(root.minZoneSize, relWidth * root.width - leftGap - rightGap)
            height: Math.max(root.minZoneSize, relHeight * root.height - topGap - bottomGap)
            // Scale on hover (only if hoverScale > 1.0)
            scale: isZoneHovered && root.hoverScale > 1.0 ? root.hoverScale : 1.0
            z: isZoneHovered ? 10 : 1
            transformOrigin: Item.Center
            // Zone fill color - use highlight color when selected/hovered, inactive color otherwise
            color: {
                var isHighlighted = root.isActive || root.isHovered || isZoneSelected || isZoneHovered;
                if (isHighlighted) {
                    return root.highlightColor;
                }
                return root.inactiveColor;
            }
            opacity: (root.isActive || root.isHovered || isZoneSelected || isZoneHovered) ? root.activeOpacity : root.inactiveOpacity
            // Border - brighter on hover
            border.color: {
                if (isZoneHovered) {
                    // Brighter border when hovered
                    return Qt.rgba(
                        Math.min(1, Kirigami.Theme.highlightColor.r * 1.2),
                        Math.min(1, Kirigami.Theme.highlightColor.g * 1.2),
                        Math.min(1, Kirigami.Theme.highlightColor.b * 1.2), 1);
                }
                return root.borderColor;
            }
            border.width: (isZoneSelected || isZoneHovered) ? 2 : 1
            radius: Kirigami.Units.smallSpacing * 0.5

            // Zone number label
            Label {
                anchors.centerIn: parent
                // Use actual zoneNumber from data if available, otherwise fall back to index + 1
                text: modelData.zoneNumber !== undefined ? modelData.zoneNumber : (index + 1)
                font.pixelSize: Math.min(parent.width, parent.height) * 0.4
                font.bold: true
                color: Kirigami.Theme.textColor
                opacity: (root.isActive || root.isHovered || zoneRect.isZoneSelected || zoneRect.isZoneHovered) ? 0.9 : 0.6
                visible: root.showZoneNumbers && parent.width >= 16 && parent.height >= 16

                Behavior on opacity {
                    NumberAnimation {
                        duration: root.animationDuration
                    }

                }

            }

            // Mouse interaction for zone selection (only when interactive)
            MouseArea {
                id: zoneMouseArea

                anchors.fill: parent
                anchors.margins: -2  // Slightly larger hit area
                hoverEnabled: root.interactive
                enabled: root.interactive
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

            Behavior on opacity {
                NumberAnimation {
                    duration: root.animationDuration
                }

            }

            Behavior on scale {
                NumberAnimation {
                    duration: root.animationDuration
                    easing.type: Easing.OutBack
                    easing.overshoot: 1.2
                }

            }

            Behavior on border.color {
                ColorAnimation {
                    duration: root.animationDuration / 2
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
