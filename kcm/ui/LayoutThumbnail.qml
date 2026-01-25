// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Thumbnail preview of a layout showing zone geometries
 * Maintains monitor aspect ratio for accurate zone preview rendering
 * Uses shared ZonePreview component for consistent rendering across the application
 */
Rectangle {
    id: root

    required property var layout
    property bool isSelected: false
    readonly property real previewOpacity: 0.2 // Increased for better background contrast
    readonly property real borderOpacity: 0.9 // Increased for better border visibility
    readonly property int normalBorderWidth: Math.round(Kirigami.Units.devicePixelRatio)
    readonly property int selectedBorderWidth: Math.round(Kirigami.Units.devicePixelRatio * 2.5) // Thicker when selected
    // Get primary screen aspect ratio for accurate preview
    readonly property var primaryScreen: Screen.primaryScreen
    readonly property real screenAspectRatio: primaryScreen ? (primaryScreen.width / primaryScreen.height) : (16 / 9) // Default to 16:9 if no screen
    // Calculate dimensions based on aspect ratio.
    // Use a base height and calculate width to match screen ratio.
    readonly property real baseHeight: Kirigami.Units.gridUnit * 7
    // 56px base height (compact)
    readonly property real calculatedWidth: baseHeight * screenAspectRatio
    readonly property real minThumbnailWidth: Kirigami.Units.gridUnit * 10 // 80px minimum
    readonly property real maxThumbnailWidth: Kirigami.Units.gridUnit * 20 // 160px maximum

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
        minZoneSize: 8
        showZoneNumbers: true
    }

    // Layout name label
    Label {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Kirigami.Units.smallSpacing
        text: root.layout ? (root.layout.name || i18n("Unnamed")) : ""
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
        font.bold: isSelected
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter

        background: Rectangle {
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.9)
            radius: Kirigami.Units.smallSpacing * 0.5
        }

    }

}
