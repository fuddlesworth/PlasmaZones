// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import org.kde.kirigami as Kirigami

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
    property bool enableBlur: true
    // Ricer-friendly appearance properties - using theme colors
    property color highlightColor: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.7)
    property color inactiveColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
    property color borderColor: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.9)
    property color labelFontColor: Kirigami.Theme.textColor
    property string fontFamily: ""
    property real fontSizeScale: 1.0
    property int fontWeight: Font.Bold
    property bool fontItalic: false
    property bool fontUnderline: false
    property bool fontStrikeout: false
    property real activeOpacity: 0.5 // Match Settings default
    property real inactiveOpacity: 0.3 // Match Settings default
    property int borderWidth: Kirigami.Units.smallSpacing // 4px - increased for better visibility
    property int borderRadius: Kirigami.Units.gridUnit // 8px - use theme spacing

    signal zoneClicked(int index)
    signal zoneHovered(int index)

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

    // Window flags - LayerShellQt handles the overlay behavior on Wayland
    flags: Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // Start hidden; OverlayService controls visibility via show()/hide().
    visible: false

    // Main content item
    Item {
        id: content

        anchors.fill: parent

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

        // Zone repeater
        Repeater {
            // QML can parse hex color strings directly

            model: root.zones

            delegate: ZoneItem {
                required property var modelData
                required property int index

                // Use exact geometry from C++ - this matches snap geometry exactly
                x: modelData.x
                y: modelData.y
                width: modelData.width
                height: modelData.height
                zoneNumber: modelData.zoneNumber || (index + 1)
                zoneName: modelData.name || ""
                isHighlighted: {
                    if (modelData.isHighlighted)
                        return true;

                    if (!modelData.id)
                        return false;

                    if (modelData.id === root.highlightedZoneId)
                        return true;

                    // Check if zone ID is in multi-zone array
                    for (var i = 0; i < root.highlightedZoneIds.length; i++) {
                        if (root.highlightedZoneIds[i] === modelData.id)
                            return true;

                    }
                    return false;
                }
                isMultiZone: {
                    if (root.highlightedZoneIds.length <= 1)
                        return false;

                    if (!modelData.id)
                        return false;

                    // Check if zone ID is in multi-zone array
                    for (var i = 0; i < root.highlightedZoneIds.length; i++) {
                        if (root.highlightedZoneIds[i] === modelData.id)
                            return true;

                    }
                    return false;
                }
                showNumber: root.showNumbers
                // Use custom colors if useCustomColors is true, otherwise use theme defaults
                // Colors come as hex strings (ARGB format) from C++, QML can parse them directly
                highlightColor: {
                    // Check if useCustomColors is true (handle boolean, number, or string)
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    if (useCustom && modelData.highlightColor)
                        return modelData.highlightColor;

                    return root.highlightColor;
                }
                inactiveColor: {
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    if (useCustom && modelData.inactiveColor)
                        return modelData.inactiveColor;

                    return root.inactiveColor;
                }
                borderColor: {
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    if (useCustom && modelData.borderColor)
                        return modelData.borderColor;

                    return root.borderColor;
                }
                labelFontColor: root.labelFontColor
                fontFamily: root.fontFamily
                fontSizeScale: root.fontSizeScale
                fontWeight: root.fontWeight
                fontItalic: root.fontItalic
                fontUnderline: root.fontUnderline
                fontStrikeout: root.fontStrikeout
                // Use custom opacity if useCustomColors is true - now uses separate active/inactive opacity
                activeOpacity: {
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    return (useCustom && modelData.activeOpacity !== undefined) ? modelData.activeOpacity : root.activeOpacity;
                }
                inactiveOpacity: {
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    return (useCustom && modelData.inactiveOpacity !== undefined) ? modelData.inactiveOpacity : root.inactiveOpacity;
                }
                // Use custom border properties if useCustomColors is true
                borderWidth: {
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    return (useCustom && modelData.borderWidth !== undefined) ? modelData.borderWidth : root.borderWidth;
                }
                borderRadius: {
                    var useCustom = modelData.useCustomColors === true || modelData.useCustomColors === 1 || (typeof modelData.useCustomColors === "string" && modelData.useCustomColors.toLowerCase() === "true");
                    return (useCustom && modelData.borderRadius !== undefined) ? modelData.borderRadius : root.borderRadius;
                }
                onClicked: root.zoneClicked(index)
                onHovered: root.zoneHovered(index)
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
