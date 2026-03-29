// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Thumbnail preview of a layout showing zone geometries
 * Renders at the layout's intended aspect ratio (16:9, 21:9, 32:9, 9:16)
 * so previews accurately represent how zones will look on the target monitor type.
 * Falls back to the primary screen's aspect ratio for user-created layouts.
 * Uses shared ZonePreview component for consistent rendering across the application.
 */
Rectangle {
    id: root

    required property var layout
    property bool isSelected: false
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    readonly property real previewOpacity: 0.2 // Increased for better background contrast
    readonly property real borderOpacity: 0.9 // Increased for better border visibility
    readonly property int normalBorderWidth: Math.round(Kirigami.Units.devicePixelRatio)
    readonly property int selectedBorderWidth: Math.round(Kirigami.Units.devicePixelRatio * 2.5) // Thicker when selected
    // Aspect ratio: use the layout's intended ratio so previews show correct proportions.
    // Falls back to primary screen ratio for user-created layouts (aspectRatioClass "any" or absent).
    readonly property real fallbackAspectRatio: (Screen.width > 0 && Screen.height > 0) ? (Screen.width / Screen.height) : (16 / 9)
    readonly property real layoutAspectRatio: {
        var cls = root.layout ? (root.layout.aspectRatioClass || "any") : "any";
        switch (cls) {
        case "standard":
            return 16 / 9;
        case "ultrawide":
            return 21 / 9;
        case "super-ultrawide":
            return 32 / 9;
        case "portrait":
            return 9 / 16;
        default:
            // For "any" layouts with fixed-geometry zones, use the reference
            // aspect ratio from the screen the zones were designed for
            var refAR = root.layout ? (root.layout.referenceAspectRatio || 0) : 0;
            return refAR > 0 ? refAR : fallbackAspectRatio;
        }
    }
    // Calculate dimensions based on the layout's aspect ratio.
    // Portrait layouts use a base width (narrower) instead of base height.
    readonly property bool isPortraitLayout: layoutAspectRatio < 1
    property real baseHeight: Kirigami.Units.gridUnit * 9
    readonly property real calculatedWidth: isPortraitLayout ? baseHeight * layoutAspectRatio : baseHeight * layoutAspectRatio
    property real minThumbnailWidth: Kirigami.Units.gridUnit * 5 // Narrower min for portrait
    property real maxThumbnailWidth: Kirigami.Units.gridUnit * 26 // Wider max for super-ultrawide

    // Use calculated width but ensure minimum size for usability
    implicitWidth: Math.max(minThumbnailWidth, Math.min(calculatedWidth, maxThumbnailWidth))
    implicitHeight: baseHeight
    // Maintain aspect ratio when resized
    width: implicitWidth
    height: implicitHeight
    radius: Kirigami.Units.smallSpacing
    // Use a neutral background with better contrast for zone visibility
    // Slightly darker background helps zones stand out better
    color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, previewOpacity)
    // Strong border colors for clear boundaries
    border.color: isSelected ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, borderOpacity)
    border.width: isSelected ? selectedBorderWidth : normalBorderWidth

    // Use shared ZonePreview component for consistent zone rendering
    QFZCommon.ZonePreview {
        id: zonePreview

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing * 1.5
        anchors.bottomMargin: Kirigami.Units.gridUnit * 1.5 + Kirigami.Units.smallSpacing // Space for label
        zones: root.layout && root.layout.zones ? root.layout.zones : []
        isActive: root.isSelected
        zonePadding: 1 // Minimal padding for thumbnail
        edgeGap: 1 // Minimal edge gap for thumbnail
        minZoneSize: 8
        showZoneNumbers: true
        zoneNumberDisplay: root.layout ? (root.layout.zoneNumberDisplay || "all") : "all"
        fontFamily: root.fontFamily
        fontSizeScale: root.fontSizeScale
        fontWeight: root.fontWeight
        fontItalic: root.fontItalic
        fontUnderline: root.fontUnderline
        fontStrikeout: root.fontStrikeout
    }

    // Master indicator dots for autotile algorithms that support master count
    Repeater {
        model: root.layout && root.layout.isAutotile === true && root.layout.supportsMasterCount === true ? zonePreview.zones : []

        Rectangle {
            required property var modelData
            required property int index
            readonly property real relX: (modelData.relativeGeometry && modelData.relativeGeometry.x) || 0
            readonly property real relY: (modelData.relativeGeometry && modelData.relativeGeometry.y) || 0
            readonly property real leftOffset: relX < 0.01 ? zonePreview.edgeGap : zonePreview.zonePadding / 2
            readonly property real topOffset: relY < 0.01 ? zonePreview.edgeGap : zonePreview.zonePadding / 2

            // Default master count of 1 — grid thumbnails don't have per-layout masterCount
            visible: index < 1
            Accessible.ignored: true
            x: zonePreview.x + relX * zonePreview.width + leftOffset + Kirigami.Units.smallSpacing
            y: zonePreview.y + relY * zonePreview.height + topOffset + Kirigami.Units.smallSpacing
            width: Kirigami.Units.smallSpacing * 2
            height: Kirigami.Units.smallSpacing * 2
            radius: Kirigami.Units.smallSpacing
            color: Kirigami.Theme.positiveTextColor
        }

    }

    // Layout name label
    Label {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Kirigami.Units.smallSpacing
        text: root.layout ? (root.layout.name || i18n("Unnamed")) : ""
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        font.weight: isSelected ? Font.DemiBold : Font.Normal
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.9)
            radius: Kirigami.Units.smallSpacing * 0.5
        }

    }

}
