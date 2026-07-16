// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Zone content component for displaying zone information
 *
 * Displays zone number and name labels in the center of the zone.
 */
Item {
    id: zoneContent

    required property var zoneData
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false

    anchors.fill: parent

    // Zone number and name container - use Column for better positioning
    Column {
        id: contentColumn

        anchors.centerIn: parent
        // Scale spacing proportionally with zone size for better positioning when small
        spacing: {
            var zoneSize = Math.min(zoneContent.width || 0, zoneContent.height || 0);
            // Scale spacing from half smallSpacing (small zones) to double smallSpacing (large zones)
            return Math.max(Kirigami.Units.smallSpacing / 2, Math.min(Kirigami.Units.smallSpacing * 2, zoneSize * 0.04));
        }

        // Zone number label
        Label {
            id: numberLabel

            anchors.horizontalCenter: parent.horizontalCenter
            text: (zoneContent.zoneData && zoneContent.zoneData.zoneNumber) || 1
            // Guard against negative or zero dimensions causing invalid font size
            font.pixelSize: Math.max(Kirigami.Units.smallSpacing * 2, Math.min(zoneContent.width || 0, zoneContent.height || 0) * 0.25 * zoneContent.fontSizeScale)
            font.weight: zoneContent.fontWeight
            font.italic: zoneContent.fontItalic
            font.underline: zoneContent.fontUnderline
            font.strikeout: zoneContent.fontStrikeout
            font.family: zoneContent.fontFamily
            color: Kirigami.Theme.textColor
            opacity: 0.8
        }

        // Zone name label (below number)
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: (zoneContent.zoneData && zoneContent.zoneData.name) || ""
            // Scale font size based on zone dimensions, using default font size as base
            font.pixelSize: {
                var baseSize = Kirigami.Theme.defaultFont.pixelSize;
                var zoneSize = Math.min(zoneContent.width || 0, zoneContent.height || 0);
                var scaleFactor = zoneSize / 200; // Normalize to ~200px reference
                var scaledSize = baseSize * Math.max(0.4, Math.min(1, scaleFactor)) * zoneContent.fontSizeScale; // Scale between 40% and 100% of base
                return Math.max(Kirigami.Units.smallSpacing * 2, Math.round(scaledSize)); // Unit-derived minimum for readability
            }
            font.weight: zoneContent.fontWeight
            font.italic: zoneContent.fontItalic
            font.underline: zoneContent.fontUnderline
            font.strikeout: zoneContent.fontStrikeout
            font.family: zoneContent.fontFamily
            color: Kirigami.Theme.textColor
            opacity: 0.6
            visible: text !== ""
        }
    }
}
