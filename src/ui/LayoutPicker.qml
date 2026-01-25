// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * Layout picker bar that shows at the top of the screen during Ctrl+drag
 * Allows quick switching between layouts by hovering over thumbnails
 */
Rectangle {
    id: root

    property var layouts: [] // Array of layout objects with zones
    property string activeLayoutId: ""
    property string hoveredLayoutId: ""
    // Theme colors with opacity constants
    readonly property real borderOpacity: 0.5
    readonly property real highlightOpacity: 0.5
    readonly property real activeOpacity: 0.6
    readonly property real inactiveOpacity: 0.15
    readonly property real hoverOpacity: 0.4
    readonly property real textSecondaryOpacity: 0.5
    property color backgroundColor: Kirigami.Theme.backgroundColor
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, borderOpacity)
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, highlightOpacity)
    property color activeColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, activeOpacity)
    property int thumbnailWidth: 120 // Layout-specific dimension - acceptable
    property int thumbnailHeight: 70 // Layout-specific dimension - acceptable
    property int spacing: Kirigami.Units.gridUnit * 1.5 // Use theme spacing (12px)
    property int padding: Kirigami.Units.gridUnit * 2 // Use theme spacing (16px)

    signal layoutSelected(string layoutId)
    signal layoutHovered(string layoutId)

    // Calculate dimensions based on content
    implicitWidth: Math.min(flickable.contentWidth + padding * 2, parent ? parent.width - 100 : 800)
    implicitHeight: thumbnailHeight + padding * 2 + 8
    // Center horizontally at the top
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    color: backgroundColor
    radius: Kirigami.Units.gridUnit * 1.5 // Use theme spacing (12px)
    border.color: borderColor
    border.width: constants.standardBorderWidth
    // Show/hide animation
    opacity: visible ? 1 : 0

    // Constants for visual styling
    QtObject {
        id: constants

        readonly property int standardBorderWidth: Kirigami.Units.smallSpacing / 2 // 2px - standard border width
        readonly property int thickBorderWidth: Kirigami.Units.smallSpacing // 4px - thick border for emphasis
        readonly property int thinBorderWidth: 1 // 1px - thin border for subtle elements
    }

    // Subtle shadow effect
    Rectangle {
        anchors.fill: parent
        anchors.margins: -2
        z: -1
        color: "transparent"
        radius: Kirigami.Units.gridUnit * 1.75 // Use theme spacing (14px)
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.inactiveOpacity)
        border.width: constants.thickBorderWidth
    }

    Flickable {
        id: flickable

        anchors.fill: parent
        anchors.margins: root.padding
        contentWidth: layoutRow.width
        contentHeight: layoutRow.height
        clip: true
        interactive: contentWidth > width

        Row {
            id: layoutRow

            spacing: root.spacing

            Repeater {
                model: root.layouts

                delegate: Rectangle {
                    id: thumbnailContainer

                    required property var modelData
                    required property int index
                    property string layoutId: modelData.id || ""
                    property string layoutName: modelData.name || "Layout " + (index + 1)
                    property var zones: modelData.zones || []
                    property bool isActive: layoutId === root.activeLayoutId
                    property bool isHovered: layoutId === root.hoveredLayoutId
                    // Scale effect on hover
                    readonly property real hoverScale: 1.05

                    width: root.thumbnailWidth
                    height: root.thumbnailHeight
                    radius: Kirigami.Units.gridUnit // Use theme spacing (8px)
                    color: isHovered ? root.highlightColor : (isActive ? root.activeColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.inactiveOpacity))
                    border.color: isHovered || isActive ? Kirigami.Theme.textColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.hoverOpacity)
                    border.width: isHovered || isActive ? constants.standardBorderWidth : constants.thinBorderWidth
                    Accessible.name: i18nc("@info:accessibility", "Layout: %1", thumbnailContainer.layoutName)
                    Accessible.description: i18nc("@info:accessibility", "Click to select this layout. Contains %1 zones.", thumbnailContainer.zones.length)
                    Accessible.role: Accessible.Button
                    scale: isHovered ? hoverScale : 1

                    // Zone preview - render actual zone geometries
                    Item {
                        id: zonePreview

                        anchors.fill: parent
                        anchors.margins: Kirigami.Units.smallSpacing * 1.5 // Use theme spacing (6px)

                        Repeater {
                            model: thumbnailContainer.zones

                            delegate: Rectangle {
                                required property var modelData
                                required property int index
                                // Scale relative geometry (0-1) to thumbnail size
                                property var relGeo: modelData.relativeGeometry || {
                                }

                                x: (relGeo.x || 0) * zonePreview.width + 1
                                y: (relGeo.y || 0) * zonePreview.height + 1
                                width: Math.max(8, (relGeo.width || 0.25) * zonePreview.width - 2)
                                height: Math.max(8, (relGeo.height || 1) * zonePreview.height - 2)
                                color: thumbnailContainer.isHovered ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.hoverOpacity) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.inactiveOpacity)
                                border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.borderOpacity)
                                border.width: constants.thinBorderWidth
                                radius: Kirigami.Units.smallSpacing * 0.5 // Use theme spacing (2px)
                            }

                        }

                    }

                    // Layout name label
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.left: parent.left
                        anchors.right: parent.right
                        height: Kirigami.Units.gridUnit * 2.25 // Use theme spacing (18px)
                        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, root.borderOpacity)
                        radius: Kirigami.Units.smallSpacing // Use theme spacing

                        // Round only bottom corners
                        Rectangle {
                            anchors.top: parent.top
                            anchors.left: parent.left
                            anchors.right: parent.right
                            height: parent.radius
                            color: parent.color
                        }

                        Text {
                            anchors.centerIn: parent
                            text: thumbnailContainer.layoutName
                            color: Kirigami.Theme.textColor
                            font.pixelSize: Kirigami.Theme.smallFont.pixelSize
                            font.bold: thumbnailContainer.isActive
                            elide: Text.ElideRight
                            width: parent.width - Kirigami.Units.gridUnit
                            horizontalAlignment: Text.AlignHCenter
                        }

                    }

                    // Mouse interaction
                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        onEntered: {
                            root.hoveredLayoutId = thumbnailContainer.layoutId;
                            root.layoutHovered(thumbnailContainer.layoutId);
                        }
                        onExited: {
                            if (root.hoveredLayoutId === thumbnailContainer.layoutId)
                                root.hoveredLayoutId = "";

                        }
                        onClicked: {
                            root.layoutSelected(thumbnailContainer.layoutId);
                        }
                    }

                    ToolTip {
                        visible: thumbnailContainer.isHovered || thumbnailContainer.isActive
                        text: i18nc("@tooltip", "Select layout: %1", thumbnailContainer.layoutName)
                        delay: Kirigami.Units.toolTipDelay
                    }

                    // Hover animation
                    Behavior on color {
                        ColorAnimation {
                            duration: 150
                        }

                    }

                    Behavior on border.width {
                        NumberAnimation {
                            duration: 100
                        }

                    }

                    Behavior on scale {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutBack
                        }

                    }

                }

            }

        }

    }

    // Instruction text when no layouts
    Text {
        anchors.centerIn: parent
        text: i18n("No layouts available")
        color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, root.textSecondaryOpacity)
        font.pixelSize: Kirigami.Theme.defaultFont.pixelSize
        visible: root.layouts.length === 0
    }

    Behavior on opacity {
        NumberAnimation {
            duration: 200
            easing.type: Easing.OutQuad
        }

    }

}
