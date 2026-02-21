// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Simple tooltip showing zone dimensions during drag/resize operations
 *
 * Displays position and size (percentage) in a clean, minimal format at the bottom of the zone.
 * Similar to Windows FancyZones behavior.
 */
Rectangle {
    // 8px padding from bottom

    id: dimensionTooltip

    property real zoneX: 0
    property real zoneY: 0
    property real zoneWidth: 0
    property real zoneHeight: 0
    property real canvasWidth: 1
    property real canvasHeight: 1
    property bool showDimensions: false
    property bool isFixedMode: false
    property real screenWidth: 1920
    property real screenHeight: 1080
    // Calculate percentages (relative mode)
    property int widthPercent: (canvasWidth > 0 && !isNaN(zoneWidth)) ? Math.round((zoneWidth / canvasWidth) * 100) : 0
    property int heightPercent: (canvasHeight > 0 && !isNaN(zoneHeight)) ? Math.round((zoneHeight / canvasHeight) * 100) : 0
    property int xPercent: (canvasWidth > 0 && !isNaN(zoneX)) ? Math.round((zoneX / canvasWidth) * 100) : 0
    property int yPercent: (canvasHeight > 0 && !isNaN(zoneY)) ? Math.round((zoneY / canvasHeight) * 100) : 0
    // Calculate pixel values (fixed mode)
    property int fixedPosX: (canvasWidth > 0 && screenWidth > 0) ? Math.round((zoneX / canvasWidth) * screenWidth) : 0
    property int fixedPosY: (canvasHeight > 0 && screenHeight > 0) ? Math.round((zoneY / canvasHeight) * screenHeight) : 0
    property int fixedSizeW: (canvasWidth > 0 && screenWidth > 0) ? Math.round((zoneWidth / canvasWidth) * screenWidth) : 0
    property int fixedSizeH: (canvasHeight > 0 && screenHeight > 0) ? Math.round((zoneHeight / canvasHeight) * screenHeight) : 0
    // Position at bottom center of zone, with bounds checking
    readonly property real bottomPadding: Kirigami.Units.gridUnit

    visible: showDimensions && zoneWidth > 0 && zoneHeight > 0 && canvasWidth > 0 && canvasHeight > 0
    x: {
        var centerX = zoneX + (zoneWidth - width) / 2;
        var minX = zoneX + Kirigami.Units.smallSpacing;
        var maxX = zoneX + zoneWidth - width - Kirigami.Units.smallSpacing;
        return Math.max(minX, Math.min(maxX, centerX));
    }
    y: zoneY + zoneHeight - height - bottomPadding // Bottom of zone with proper padding
    width: contentColumn.implicitWidth + Kirigami.Units.gridUnit
    height: contentColumn.implicitHeight + Kirigami.Units.smallSpacing * 2
    radius: Kirigami.Units.smallSpacing
    // Use Tooltip colorSet for proper theme colors
    Kirigami.Theme.inherit: false
    Kirigami.Theme.colorSet: Kirigami.Theme.Tooltip
    color: Kirigami.Theme.backgroundColor
    border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
    border.width: constants.tooltipBorderWidth
    z: 250
    Accessible.name: i18nc("@info:accessibility", "Zone dimensions")
    Accessible.description: isFixedMode
        ? i18nc("@info:accessibility", "Position: %1px, %2px  Size: %3px × %4px", fixedPosX, fixedPosY, fixedSizeW, fixedSizeH)
        : i18nc("@info:accessibility", "Position: %1%, %2%  Size: %3% × %4%", xPercent, yPercent, widthPercent, heightPercent)

    // Constants for visual styling
    QtObject {
        id: constants

        readonly property int tooltipBorderWidth: 1 // 1px - tooltip border width
    }

    GridLayout {
        id: contentColumn

        anchors.centerIn: parent
        columns: 2
        columnSpacing: Kirigami.Units.smallSpacing
        rowSpacing: Kirigami.Units.smallSpacing / 2

        // Row 1: Position
        Label {
            text: i18nc("@label", "Pos:")
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            color: Kirigami.Theme.disabledTextColor
        }

        Label {
            text: isFixedMode
                ? i18nc("@info Position in pixels", "%1px, %2px", fixedPosX, fixedPosY)
                : i18nc("@info Position as percentages", "%1% × %2%", xPercent, yPercent)
            font.family: "monospace"
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
            color: Kirigami.Theme.textColor
        }

        // Row 2: Size
        Label {
            text: i18nc("@label", "Size:")
            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
            color: Kirigami.Theme.disabledTextColor
        }

        Label {
            text: isFixedMode
                ? i18nc("@info Size in pixels", "%1px × %2px", fixedSizeW, fixedSizeH)
                : i18nc("@info Size as percentages", "%1% × %2%", widthPercent, heightPercent)
            font.family: "monospace"
            font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
            color: Kirigami.Theme.textColor
        }

    }

}
