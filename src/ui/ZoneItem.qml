// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.common as QFZCommon

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
    property color highlightColor: QFZCommon.ZoneColorDefaults.activeZoneColor
    property color inactiveColor: QFZCommon.ZoneColorDefaults.inactiveZoneColor
    property color borderColor: QFZCommon.ZoneColorDefaults.zoneBorderColor
    property color labelFontColor: Kirigami.Theme.textColor
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property real activeOpacity: 0.5 // Match Settings default
    property real inactiveOpacity: 0.3 // Match Settings default
    property int borderWidth: Kirigami.Units.smallSpacing // 4px - increased for better visibility
    property int borderRadius: Kirigami.Units.gridUnit // 8px - use theme spacing

    signal clicked
    signal hovered

    // Zone background
    Rectangle {
        id: background

        anchors.fill: parent
        radius: zoneItem.borderRadius
        // Discard the fill colour's own alpha so `opacity` is the SOLE alpha
        // control — otherwise the two multiply (colour.a × opacity) and the
        // zone renders far more transparent than the configured opacity. This
        // matches the shader overlay path (overlay_data.cpp sets FillA =
        // activeOpacity, ignoring the colour's alpha) and SnapAssistContent.
        color: {
            var base = zoneItem.isHighlighted ? zoneItem.highlightColor : zoneItem.inactiveColor;
            return Qt.rgba(base.r, base.g, base.b, 1.0);
        }
        opacity: zoneItem.isHighlighted ? zoneItem.activeOpacity : zoneItem.inactiveOpacity
        // Multi-zone: increase border width by 2px, brighter border color
        border.width: zoneItem.isMultiZone ? (zoneItem.borderWidth + 2) : zoneItem.borderWidth
        border.color: {
            if (zoneItem.isMultiZone && zoneItem.isHighlighted) {
                // Blend border color toward the highlight for a brighter multi-zone edge
                return Qt.tint(zoneItem.borderColor, Qt.alpha(zoneItem.highlightColor, 0.3));
            }
            return zoneItem.borderColor;
        }

        // Phase 4: zone highlight transitions use the user's active
        // animation Profile via the "widget.zoneHighlight" registry path.
        // Daemon publishes Settings::animationProfile() here; live-updates
        // on settings edit with no daemon restart.
        Behavior on color {
            PhosphorMotionAnimation {
                profile: "widget.zoneHighlight"
            }
        }

        Behavior on opacity {
            PhosphorMotionAnimation {
                profile: "widget.zoneHighlight"
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
            font.pixelSize: Math.min(zoneItem.width, zoneItem.height) * 0.3 * zoneItem.fontSizeScale
            font.weight: zoneItem.fontWeight
            font.italic: zoneItem.fontItalic
            font.underline: zoneItem.fontUnderline
            font.strikeout: zoneItem.fontStrikeout
            font.family: zoneItem.fontFamily
            color: zoneItem.labelFontColor
            opacity: zoneItem.isHighlighted ? 1 : 0.7

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight"
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
            color: zoneItem.labelFontColor
            opacity: zoneItem.isHighlighted ? 0.9 : 0.5
            visible: zoneItem.zoneName.length > 0

            Behavior on opacity {
                PhosphorMotionAnimation {
                    profile: "widget.zoneHighlight"
                }
            }
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
