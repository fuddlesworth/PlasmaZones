// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Zone number/name label overlay.
 * Used by RenderNodeOverlay to display zone identifiers on top of shader effects.
 * Renders centered text with an outline for visibility on any background.
 */
Item {
    id: root

    property int zoneNumber: 1
    property string zoneName: ""
    property color numberColor: Kirigami.Theme.textColor

    // Centered label with outline for visibility
    // Always show zone number for keyboard navigation reference
    // Zone names are intentionally not shown in the overlay to keep it clean
    Label {
        anchors.centerIn: parent
        text: root.zoneNumber.toString()
        color: root.numberColor
        font.pixelSize: Math.max(Kirigami.Units.gridUnit, Math.min(parent.width, parent.height) * 0.25)
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        // Text outline for visibility on any background
        // Note: Using explicit black/white for contrast is intentional here - the luminance
        // calculation determines which provides better readability regardless of theme.
        // This is a common accessibility pattern for text overlays on variable backgrounds.
        style: Text.Outline
        styleColor: {
            // Use contrasting color for outline based on luminance
            // Guard against undefined/null numberColor during initialization
            var c = root.numberColor || Qt.rgba(1, 1, 1, 1)
            var luminance = c.r * 0.299 + c.g * 0.587 + c.b * 0.114
            // Dark outline for light text, light outline for dark text
            return luminance > 0.5
                ? Qt.rgba(Kirigami.Theme.backgroundColor.r * 0.2, Kirigami.Theme.backgroundColor.g * 0.2, Kirigami.Theme.backgroundColor.b * 0.2, 0.8)
                : Qt.rgba(1.0 - Kirigami.Theme.backgroundColor.r * 0.2, 1.0 - Kirigami.Theme.backgroundColor.g * 0.2, 1.0 - Kirigami.Theme.backgroundColor.b * 0.2, 0.8)
        }
    }
}
