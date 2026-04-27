// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import org.phosphor.animation
import org.plasmazones.common as QFZCommon

/**
 * Mini layout preview component for the Zone Selector
 * Renders zone rectangles proportionally within a fixed-size preview
 * Uses shared ZonePreview component for consistent rendering across the application
 */
Rectangle {
    id: root

    // Layout identification
    property string layoutId: ""
    property string layoutName: ""
    property var zones: [] // Array of zone objects with relativeGeometry
    property int category: 0 // 0=Manual (matches LayoutCategory in C++)
    property bool autoAssign: false
    // Global "Auto-assign for all layouts" master toggle (#370) — passed by the
    // parent so the badge shows effective rather than per-layout state.
    property bool globalAutoAssign: false
    // Autotile algorithm metadata
    property bool showMasterDot: false
    property bool producesOverlappingZones: false
    property string zoneNumberDisplay: "all"
    // State
    property bool isActive: false
    property bool isHovered: false
    // Dimensions
    property int previewWidth: 130
    property int previewHeight: 70
    // Theme colors
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5)
    property color activeColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
    property real inactiveOpacity: 0.2
    property real hoverOpacity: 0.4
    // Font properties for zone number labels
    property string fontFamily: ""
    property real fontSizeScale: 1
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property bool locked: false
    // Scale effect on hover
    readonly property real hoverScale: 1.05

    // Signals
    signal clicked()
    signal hovered()
    signal unhovered()

    // Size
    width: previewWidth
    height: previewHeight + labelContainer.height
    // Background
    color: {
        if (isHovered)
            return highlightColor;

        if (isActive)
            return activeColor;

        return Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, inactiveOpacity);
    }
    radius: Kirigami.Units.gridUnit // 8px
    border.color: isHovered || isActive ? Kirigami.Theme.textColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, hoverOpacity)
    border.width: isHovered || isActive ? constants.standardBorderWidth : constants.thinBorderWidth
    scale: isHovered ? hoverScale : 1
    // Accessibility
    Accessible.name: i18nc("@info:accessibility", "Layout: %1", root.layoutName)
    Accessible.description: i18nc("@info:accessibility", "Click to select this layout. Contains %1 zones.", root.zones.length)
    Accessible.role: Accessible.Button

    // Constants for visual styling
    QtObject {
        id: constants

        readonly property int standardBorderWidth: Kirigami.Units.smallSpacing / 2 // 2px
        readonly property int thinBorderWidth: 1
        readonly property int animationDuration: 150 // ms
    }

    // Use shared ZonePreview component for consistent zone rendering
    QFZCommon.ZonePreview {
        id: zoneContainer

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing * 1.5 // 6px
        anchors.bottomMargin: labelContainer.height + Kirigami.Units.smallSpacing
        zones: root.zones
        isHovered: root.isHovered
        isActive: root.isActive
        zonePadding: 1
        edgeGap: 1
        minZoneSize: 8
        showZoneNumbers: true
        zoneNumberDisplay: root.zoneNumberDisplay
        producesOverlappingZones: root.producesOverlappingZones
        inactiveOpacity: root.inactiveOpacity
        activeOpacity: root.hoverOpacity
        fontFamily: root.fontFamily
        fontSizeScale: root.fontSizeScale
        fontWeight: root.fontWeight
        fontItalic: root.fontItalic
        fontUnderline: root.fontUnderline
        fontStrikeout: root.fontStrikeout
        showMasterDot: root.showMasterDot
        animationDuration: constants.animationDuration
    }

    // Layout name label at bottom with category badge
    Rectangle {
        id: labelContainer

        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: Kirigami.Units.gridUnit * 2.25 // 18px
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.7)
        radius: Kirigami.Units.smallSpacing

        // Round only bottom corners (cover top corners with rect)
        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: parent.radius
            color: parent.color
        }

        // Row containing category badge and layout name
        Row {
            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing

            // Category badge (layout type) - inline with name
            QFZCommon.CategoryBadge {
                id: categoryBadge

                anchors.verticalCenter: parent.verticalCenter
                category: root.category
                autoAssign: root.autoAssign === true
                globalAutoAssign: root.globalAutoAssign
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: root.layoutName
                color: Kirigami.Theme.textColor
                font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                font.bold: root.isActive
                elide: Text.ElideRight
                width: Math.min(implicitWidth, labelContainer.width - categoryBadge.width - Kirigami.Units.gridUnit * 1.5)
            }

        }

    }

    // Active indicator badge
    Rectangle {
        id: activeBadge

        anchors.top: parent.top
        anchors.right: parent.right
        anchors.topMargin: Kirigami.Units.smallSpacing
        anchors.rightMargin: Kirigami.Units.smallSpacing
        width: Kirigami.Units.gridUnit
        height: Kirigami.Units.gridUnit
        radius: width / 2
        color: Kirigami.Theme.highlightColor
        visible: root.isActive
        opacity: root.isActive ? 1 : 0

        // Checkmark icon
        Label {
            anchors.centerIn: parent
            text: "\u2713" // Unicode checkmark
            font.pixelSize: parent.width * 0.6
            font.bold: true
            color: Kirigami.Theme.highlightedTextColor
        }

        Behavior on opacity {
            PhosphorMotionAnimation {
                profile: "zone.highlight"
            }

        }

    }

    // Lock overlay (shown on non-active layouts when screen is locked)
    Rectangle {
        anchors.fill: parent
        visible: root.locked
        z: 100
        color: Qt.rgba(0, 0, 0, 0.5)
        radius: parent.radius

        Kirigami.Icon {
            anchors.centerIn: parent
            source: "object-locked"
            width: Math.min(parent.width, parent.height) * 0.3
            height: width
            color: "white"
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.ForbiddenCursor
            onClicked: function(mouse) {
                mouse.accepted = true;
            }
            onPressed: function(mouse) {
                mouse.accepted = true;
            }
        }

    }

    // Mouse interaction
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        enabled: !root.locked
        cursorShape: Qt.PointingHandCursor
        onEntered: {
            root.isHovered = true;
            root.hovered();
        }
        onExited: {
            root.isHovered = false;
            root.unhovered();
        }
        onClicked: {
            root.clicked();
        }
    }

    // Tooltip
    ToolTip {
        visible: root.isHovered
        text: root.locked ? i18nc("@tooltip", "Locked") : root.isActive ? i18nc("@tooltip", "Current layout: %1", root.layoutName) : i18nc("@tooltip", "Select layout: %1", root.layoutName)
        delay: Kirigami.Units.toolTipDelay
    }

    // Color animation
    Behavior on color {
        PhosphorMotionAnimation {
            profile: "zone.highlight"
        }

    }

    // Border animation — half-duration profile for snappier border feedback
    // (matches the original `duration: animationDuration / 2` pattern).
    Behavior on border.width {
        PhosphorMotionAnimation {
            profile: "zone.highlight-border"
        }

    }

    // Scale uses zone.highlight-pop for the OutBack overshoot=1.20 feel.
    Behavior on scale {
        PhosphorMotionAnimation {
            profile: "zone.highlight-pop"
        }

    }

}
