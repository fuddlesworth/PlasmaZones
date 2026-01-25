// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * Individual zone display component
 */
Item {
    id: zoneItem

    property int zoneNumber: 1
    property string zoneName: ""
    property bool isHighlighted: false
    property bool isMultiZone: false // True if this zone is part of a multi-zone selection
    property bool showNumber: true
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property color numberColor: Kirigami.Theme.textColor
    property real activeOpacity: 0.5 // Match Settings default
    property real inactiveOpacity: 0.3 // Match Settings default
    property int borderWidth: Kirigami.Units.smallSpacing // 4px - increased for better visibility
    property int borderRadius: Kirigami.Units.gridUnit // 8px - use theme spacing

    signal clicked()
    signal hovered()

    // Zone background
    Rectangle {
        id: background

        anchors.fill: parent
        radius: zoneItem.borderRadius
        color: zoneItem.isHighlighted ? zoneItem.highlightColor : zoneItem.inactiveColor
        opacity: zoneItem.isHighlighted ? zoneItem.activeOpacity : zoneItem.inactiveOpacity
        // Multi-zone: increase border width by 2px, brighter border color
        border.width: zoneItem.isMultiZone ? (zoneItem.borderWidth + 2) : zoneItem.borderWidth
        border.color: {
            if (zoneItem.isMultiZone && zoneItem.isHighlighted) {
                // 20% increased brightness for multi-zone - blend with highlight color
                var baseColor = zoneItem.borderColor;
                var highlightColor = zoneItem.highlightColor;
                // Mix border color with highlight color for brighter appearance
                return Qt.rgba(Math.min(1, baseColor.r * 0.7 + highlightColor.r * 0.3), Math.min(1, baseColor.g * 0.7 + highlightColor.g * 0.3), Math.min(1, baseColor.b * 0.7 + highlightColor.b * 0.3), baseColor.a);
            }
            return zoneItem.borderColor;
        }

        Behavior on color {
            ColorAnimation {
                duration: 150
            }

        }

        Behavior on opacity {
            NumberAnimation {
                duration: 150
            }

        }

    }

    // Zone number/name label
    Column {
        id: contentColumn

        anchors.centerIn: parent
        // Scale spacing proportionally with zone size for better positioning when small
        spacing: {
            var zoneSize = Math.min(zoneItem.width, zoneItem.height);
            // Scale spacing from 2px (small zones) to 4px (large zones)
            return Math.max(2, Math.min(4, zoneSize * 0.02));
        }
        visible: zoneItem.showNumber

        Label {
            id: numberLabel

            anchors.horizontalCenter: parent.horizontalCenter
            text: zoneItem.zoneNumber
            font.pixelSize: Math.min(zoneItem.width, zoneItem.height) * 0.3
            font.bold: true
            color: zoneItem.numberColor
            opacity: zoneItem.isHighlighted ? 1 : 0.7

            Behavior on opacity {
                NumberAnimation {
                    duration: 150
                }

            }

        }

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: zoneItem.zoneName
            // Scale font size based on zone dimensions, using default font size as base
            font.pixelSize: {
                var baseSize = Kirigami.Theme.defaultFont.pixelSize;
                var scaleFactor = Math.min(zoneItem.width, zoneItem.height) / 200; // Normalize to ~200px reference
                var scaledSize = baseSize * Math.max(0.4, Math.min(1, scaleFactor)); // Scale between 40% and 100% of base
                return Math.max(8, Math.round(scaledSize)); // Minimum 8px for readability
            }
            color: zoneItem.numberColor
            opacity: zoneItem.isHighlighted ? 0.9 : 0.5
            visible: zoneItem.zoneName.length > 0

            Behavior on opacity {
                NumberAnimation {
                    duration: 150
                }

            }

        }

    }

    // Keyboard shortcut hint
    Rectangle {
        width: shortcutLabel.width + Kirigami.Units.gridUnit * 2
        height: shortcutLabel.height + Kirigami.Units.gridUnit
        radius: Kirigami.Units.smallSpacing // Use theme spacing
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.7)
        visible: zoneItem.isHighlighted && zoneItem.zoneNumber <= 9

        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            bottomMargin: Kirigami.Units.gridUnit // Use theme spacing
        }

        Label {
            id: shortcutLabel

            anchors.centerIn: parent
            text: i18n("Meta+Alt+%1", zoneItem.zoneNumber)
            font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.6875) // ~11px for default size - use theme font
            color: Kirigami.Theme.textColor
        }

    }

    // Mouse interaction
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        onClicked: zoneItem.clicked()
        onEntered: zoneItem.hovered()
    }

}
