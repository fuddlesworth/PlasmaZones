// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami
import org.plasmazones.common as QFZCommon

/**
 * Zone overlay window that displays zones during window drag
 * Uses specific window flags for Wayland overlay support
 */
Window {
    // zonePadding isn't used for zone positioning. Zones render at exact snap geometry.
    // Visual spacing comes from layout.zoneSpacing in C++ GeometryUtils.

    id: root

    property var zones: []
    property string highlightedZoneId: "" // Use zone ID instead of index for stable selection (single zone)
    property var highlightedZoneIds: [] // Array of zone IDs for multi-zone highlighting (set by C++ via QQmlProperty)
    property bool showNumbers: true
    property var previewZones: [] // Full zone list with relative geometries for LayoutPreview mode
    // Ricer-friendly appearance properties - using theme colors
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
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
    // Idle state: drag-end leaves the QQuickWindow alive (avoids the
    // NVIDIA vkDestroyDevice deadlock — see OverlayService::setIdleForDragPause)
    // but we still need the overlay to visually disappear and stop
    // absorbing pointer input until the next drag. Binding
    // Qt.WindowTransparentForInput into `flags` gets Qt Wayland to call
    // wl_surface.set_input_region with an empty region in-place, without
    // destroying the surface. Toggling content.visible stops the scene
    // graph from submitting new frames. C++ flips this via
    // writeQmlProperty(window, "_idled", true/false).
    property bool _idled: false

    function flash() {
        flashAnimation.start();
    }

    function highlightZone(zoneId) {
        highlightedZoneId = zoneId || "";
        highlightedZoneIds = [];
    }

    function highlightZones(zoneIds) {
        highlightedZoneIds = zoneIds || [];
        highlightedZoneId = "";
    }

    function clearHighlight() {
        highlightedZoneId = "";
        highlightedZoneIds = [];
    }

    function hasCustomColors(zoneData) {
        var v = zoneData.useCustomColors;
        return v === true || v === 1 || (typeof v === "string" && v.toLowerCase() === "true");
    }

    function isZoneHighlighted(zoneData) {
        if (zoneData.isHighlighted)
            return true;

        if (!zoneData.id)
            return false;

        if (zoneData.id === highlightedZoneId)
            return true;

        for (var i = 0; i < highlightedZoneIds.length; i++) {
            if (highlightedZoneIds[i] === zoneData.id)
                return true;

        }
        return false;
    }

    // Window flags - QPA layer-shell plugin handles the overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus | (root._idled ? Qt.WindowTransparentForInput : 0)
    color: "transparent"
    // Start hidden; OverlayService controls visibility via show()/hide().
    visible: false

    // Main content item
    Item {
        id: content

        anchors.fill: parent
        visible: !root._idled

        // Debug background to verify overlay is showing
        Rectangle {
            id: debugBg

            anchors.fill: parent
            color: "transparent"
            visible: root.zones.length === 0

            // Show a subtle indicator when no zones
            Text {
                anchors.centerIn: parent
                text: i18n("PlasmaZones Overlay Active\n(No zones defined)")
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.5)
                font.pixelSize: Kirigami.Theme.defaultFont.pixelSize * 1.5
                horizontalAlignment: Text.AlignHCenter
                visible: parent.visible
            }

        }

        // Zone repeater — conditionally renders ZoneItem (mode 0) or LayoutPreview thumbnail (mode 1)
        Repeater {
            model: root.zones

            delegate: Loader {
                required property var modelData
                required property int index

                // Position at the zone's exact geometry regardless of display mode
                x: modelData.x
                y: modelData.y
                width: modelData.width
                height: modelData.height
                // Switch component based on resolved overlayDisplayMode (0=Rectangles, 1=LayoutPreview)
                sourceComponent: (modelData.overlayDisplayMode === 1) ? layoutPreviewComponent : zoneRectComponent
            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // Mode 0: Standard zone rectangle (current behavior)
        // ═══════════════════════════════════════════════════════════════════════
        Component {
            id: zoneRectComponent

            ZoneItem {
                // Access Loader's required properties (Loader is the parent)
                property var modelData: parent ? parent.modelData : ({
                })
                property int zoneIndex: parent ? parent.index : 0
                property bool useCustom: root.hasCustomColors(modelData)

                anchors.fill: parent
                zoneNumber: modelData.zoneNumber || (zoneIndex + 1)
                zoneName: modelData.name || ""
                isHighlighted: root.isZoneHighlighted(modelData)
                isMultiZone: {
                    if (root.highlightedZoneIds.length <= 1)
                        return false;

                    if (!modelData.id)
                        return false;

                    for (var i = 0; i < root.highlightedZoneIds.length; i++) {
                        if (root.highlightedZoneIds[i] === modelData.id)
                            return true;

                    }
                    return false;
                }
                showNumber: root.showNumbers
                highlightColor: (useCustom && modelData.highlightColor) ? modelData.highlightColor : root.highlightColor
                inactiveColor: (useCustom && modelData.inactiveColor) ? modelData.inactiveColor : root.inactiveColor
                borderColor: (useCustom && modelData.borderColor) ? modelData.borderColor : root.borderColor
                labelFontColor: root.labelFontColor
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                fontWeight: root.fontWeight
                fontItalic: root.fontItalic
                fontUnderline: root.fontUnderline
                fontStrikeout: root.fontStrikeout
                activeOpacity: (useCustom && modelData.activeOpacity !== undefined) ? modelData.activeOpacity : root.activeOpacity
                inactiveOpacity: (useCustom && modelData.inactiveOpacity !== undefined) ? modelData.inactiveOpacity : root.inactiveOpacity
                borderWidth: (useCustom && modelData.borderWidth !== undefined) ? modelData.borderWidth : root.borderWidth
                borderRadius: (useCustom && modelData.borderRadius !== undefined) ? modelData.borderRadius : root.borderRadius
            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // Mode 1: Layout preview thumbnail (kZones-style)
        // ═══════════════════════════════════════════════════════════════════════
        Component {
            id: layoutPreviewComponent

            Item {
                // Access Loader's required properties (Loader is the parent)
                property var modelData: parent ? parent.modelData : ({
                })
                property int zoneIndex: parent ? parent.index : 0

                anchors.fill: parent

                // Centered preview thumbnail with themed card background
                Item {
                    id: previewContainer

                    // Size: 60% of zone width capped at 200px, height from aspect ratio
                    property real previewWidth: Math.min(parent.width * 0.6, 200)
                    property real screenAspect: (root.width > 0 && root.height > 0) ? (root.width / root.height) : (16 / 9)
                    property real previewHeight: previewWidth / screenAspect

                    anchors.centerIn: parent
                    width: previewWidth
                    height: previewHeight

                    // Card background
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Kirigami.Units.smallSpacing
                        radius: Kirigami.Units.smallSpacing * 1.5
                        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.85)
                        border.color: Qt.rgba(root.borderColor.r, root.borderColor.g, root.borderColor.b, 0.15)
                        border.width: 1
                    }

                    // Static ZonePreview — always shows this zone highlighted
                    QFZCommon.ZonePreview {
                        anchors.fill: parent
                        zones: root.previewZones
                        selectedZoneIndex: zoneIndex
                        showZoneNumbers: root.showNumbers
                        zonePadding: 1
                        edgeGap: 1
                        minZoneSize: 8
                        activeOpacity: root.activeOpacity
                        inactiveOpacity: root.inactiveOpacity
                        highlightColor: root.highlightColor
                        inactiveColor: root.inactiveColor
                        borderColor: root.borderColor
                        labelFontColor: root.labelFontColor
                        fontFamily: root.fontFamily
                        fontSizeScale: root.fontSizeScale
                        fontWeight: root.fontWeight
                        fontItalic: root.fontItalic
                        fontUnderline: root.fontUnderline
                        fontStrikeout: root.fontStrikeout
                    }

                }

            }

        }

        // Flash animation when switching layouts
        Rectangle {
            id: flashOverlay

            anchors.fill: parent
            color: root.highlightColor
            opacity: 0
            visible: opacity > 0

            SequentialAnimation {
                id: flashAnimation

                NumberAnimation {
                    target: flashOverlay
                    property: "opacity"
                    from: 0.3
                    to: 0
                    duration: 300
                    easing.type: Easing.OutQuad
                }

            }

        }

    }

}
