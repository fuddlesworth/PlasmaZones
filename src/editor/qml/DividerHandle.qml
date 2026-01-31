// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Individual divider handle for zone resizing
 *
 * Renders a single divider between zones with grip pattern and handles
 * mouse drag operations. Emits signals to parent for state management.
 */
Rectangle {
    id: dividerHandle

    // Required properties from parent
    required property var dividerInfo
    required property int index
    required property real spacing
    required property Item drawingArea
    required property Repeater zonesRepeater

    // Signals to parent for state coordination
    signal dragStarted(var zoneStartPositions)
    signal dragMoved(real newPosition)
    signal dragEnded(real finalPosition, real originalPosition)
    signal dragCancelled()

    // Derived properties from dividerInfo
    property bool isVertical: dividerInfo ? dividerInfo.isVertical : false
    property real dividerPosition: dividerInfo ? dividerInfo.position : 0
    property var affectedZones: dividerInfo ? dividerInfo.affectedZones : []

    // Drag state for smooth visual updates
    property bool isDragging: false
    property real dragPosition: dividerPosition // Current position during drag
    property var zoneStartPositions: ({}) // Store initial zone positions

    // Use dragPosition if it differs significantly from dividerInfo.position
    // This handles the case where zones haven't updated yet after release
    // Threshold 0.02 matches roundToThreshold precision (0.01) with margin
    property bool shouldUseDragPosition: isDragging || (dividerInfo && Math.abs(dividerInfo.position - dragPosition) > 0.02)

    // Position divider in the gap between displayed zones
    // Zones are displayed with x+spacing/2, so divider must align with that
    // Use dragPosition if dragging or if dividerInfo hasn't caught up yet
    x: dividerInfo ? (dividerInfo.isVertical ? (shouldUseDragPosition ? dragPosition * drawingArea.width - spacing / 2 : dividerInfo.x - spacing / 2) : dividerInfo.x + spacing / 2) : 0
    y: dividerInfo ? (dividerInfo.isVertical ? dividerInfo.y + spacing / 2 : (shouldUseDragPosition ? dragPosition * drawingArea.height - spacing / 2 : dividerInfo.y - spacing / 2)) : 0
    width: dividerInfo ? (dividerInfo.isVertical ? spacing : dividerInfo.width - spacing) : 0 // Horizontal: span minus margins
    height: dividerInfo ? (dividerInfo.isVertical ? dividerInfo.height - spacing : spacing) : 0 // Horizontal divider height = spacing

    // Visibility depends on valid info, dimensions, and parent dragging state
    // visible property will be set by parent based on global isDragging state

    // Background - subtle base color, more visible on hover/drag
    color: (dividerMouseArea.containsMouse || isDragging) ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, isDragging ? 0.4 : 0.25) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
    border.color: (dividerMouseArea.containsMouse || isDragging) ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
    border.width: isDragging ? 2 : (dividerMouseArea.containsMouse ? 1 : 0)

    // Fully rounded ends (pill shape) - match zone border radius style
    radius: isVertical ? (width / 2) : (height / 2)

    // Dividers stay below zones (z:60) and well below resize handles (z:200+ within zones)
    // Only raise slightly when dragging to provide visual feedback
    z: isDragging ? 45 : 40

    // Sync dragPosition to dividerPosition when not dragging (e.g. after undo/redo or
    // any external zone change). Without this, undo/redo leaves dragPosition stale so
    // shouldUseDragPosition stays true and the handle is drawn at the wrong position.
    onDividerPositionChanged: {
        if (!isDragging && dividerInfo)
            dragPosition = dividerPosition;
    }

    // Grip pattern - always visible but more prominent on hover/drag
    Item {
        id: gripPattern

        // Dot properties
        readonly property real dotSize: Kirigami.Units.smallSpacing * 0.75
        readonly property real dotSpacing: dotSize * 2.5

        // Calculate number of dots - use a reasonable number that scales with size
        property int dotCount: {
            var size = dividerHandle.isVertical ? gripPattern.height : gripPattern.width;
            var count = Math.max(3, Math.min(7, Math.floor(size / dotSpacing)));
            return count;
        }

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing

        // Grip dots pattern - create a line of dots along the divider
        Repeater {
            model: gripPattern.dotCount

            Rectangle {
                required property int index
                property bool isVertical: dividerHandle.isVertical
                property int totalDots: gripPattern.dotCount
                property real dotSize: gripPattern.dotSize
                property real dotSpacing: gripPattern.dotSpacing

                // Position dots along the divider, centered
                // For vertical: center horizontally, distribute vertically
                // For horizontal: distribute horizontally, center vertically
                x: isVertical ? (gripPattern.width / 2 - dotSize / 2) : (index * dotSpacing + (gripPattern.width - (totalDots - 1) * dotSpacing) / 2 - dotSize / 2)
                y: isVertical ? (index * dotSpacing + (gripPattern.height - (totalDots - 1) * dotSpacing) / 2 - dotSize / 2) : (gripPattern.height / 2 - dotSize / 2)
                width: dotSize
                height: dotSize
                radius: dotSize / 2 // Perfect circles

                // Color based on hover/drag state
                color: (dividerMouseArea.containsMouse || dividerHandle.isDragging) ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4)
                // More visible when hovered/dragging
                opacity: (dividerMouseArea.containsMouse || dividerHandle.isDragging) ? 0.9 : 0.5

                Behavior on opacity {
                    NumberAnimation {
                        duration: 150
                    }
                }

                Behavior on color {
                    ColorAnimation {
                        duration: 150
                    }
                }
            }
        }
    }

    // Center line indicator - more prominent on hover/drag
    Rectangle {
        anchors.centerIn: parent
        width: dividerHandle.isVertical ? 2 : parent.width * 0.6
        height: dividerHandle.isVertical ? parent.height * 0.6 : 2
        radius: Math.round(Kirigami.Units.smallSpacing / 4)
        color: (dividerMouseArea.containsMouse || dividerHandle.isDragging) ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3)
        opacity: (dividerMouseArea.containsMouse || dividerHandle.isDragging) ? 0.8 : 0.4

        Behavior on opacity {
            NumberAnimation {
                duration: 150
            }
        }
    }

    MouseArea {
        id: dividerMouseArea

        property real startMousePos: 0
        property real startDividerPos: 0

        anchors.fill: parent
        anchors.margins: -4 // Extend hit area for easier grabbing
        hoverEnabled: true
        cursorShape: dividerHandle.isVertical ? Qt.SizeHorCursor : Qt.SizeVerCursor
        acceptedButtons: Qt.LeftButton
        preventStealing: true
        z: 41

        // Accessibility properties for divider controls
        Accessible.role: Accessible.Slider
        Accessible.name: dividerHandle.isVertical ? i18nc("@action:button", "Vertical zone divider") : i18nc("@action:button", "Horizontal zone divider")
        Accessible.description: dividerHandle.isVertical ? i18nc("@info:tooltip", "Drag horizontally to resize adjacent zones") : i18nc("@info:tooltip", "Drag vertically to resize adjacent zones")

        onPressed: function(mouse) {
            mouse.accepted = true;
            dividerHandle.isDragging = true;
            preventStealing = true;

            // Get initial mouse position in canvas coordinates
            var mouseInCanvas = dividerMouseArea.mapToItem(dividerHandle.drawingArea, mouse.x, mouse.y);
            startMousePos = dividerHandle.isVertical ? mouseInCanvas.x : mouseInCanvas.y;
            startDividerPos = dividerHandle.dividerPosition;
            dividerHandle.dragPosition = startDividerPos;

            // Capture starting positions of all affected zones from the zonesRepeater
            var startPos = {};
            for (var i = 0; i < dividerHandle.zonesRepeater.count; i++) {
                var zoneItem = dividerHandle.zonesRepeater.itemAt(i);
                if (!zoneItem) continue;

                var zoneId = zoneItem.zoneId;
                if (dividerHandle.affectedZones.indexOf(zoneId) >= 0) {
                    // Get zone data from the zone item's zoneData property
                    var z = zoneItem.zoneData;
                    if (z) {
                        startPos[zoneId] = {
                            "x": z.x,
                            "y": z.y,
                            "width": z.width,
                            "height": z.height
                        };
                    }
                }
            }
            dividerHandle.zoneStartPositions = startPos;

            // Emit signal to notify parent of drag start
            dividerHandle.dragStarted(startPos);
        }

        onPositionChanged: function(mouse) {
            if (!pressed || !dividerHandle.isDragging)
                return;

            var mouseInCanvas = dividerMouseArea.mapToItem(dividerHandle.drawingArea, mouse.x, mouse.y);
            var currentMousePos = dividerHandle.isVertical ? mouseInCanvas.x : mouseInCanvas.y;
            var canvasSize = dividerHandle.isVertical ? dividerHandle.drawingArea.width : dividerHandle.drawingArea.height;

            // Calculate new position
            var delta = (currentMousePos - startMousePos) / canvasSize;
            var requestedPos = startDividerPos + delta;

            // Clamp to valid range (0.05 to 0.95) to prevent impossible positions
            var newPos = Math.max(0.05, Math.min(0.95, requestedPos));
            var posDelta = newPos - startDividerPos;

            dividerHandle.dragPosition = newPos;

            // Directly update zone visuals (no C++ round-trip)
            for (var i = 0; i < dividerHandle.zonesRepeater.count; i++) {
                var zoneItem = dividerHandle.zonesRepeater.itemAt(i);
                if (!zoneItem)
                    continue;

                var zoneId = zoneItem.zoneId;
                var startData = dividerHandle.zoneStartPositions[zoneId];
                if (!startData)
                    continue;

                if (dividerHandle.isVertical) {
                    // Vertical divider: adjust x/width
                    var zoneRight = startData.x + startData.width;
                    var zoneLeft = startData.x;
                    if (Math.abs(zoneRight - startDividerPos) < 0.02) {
                        // Zone is to the LEFT of divider - adjust width
                        var newWidth = startData.width + posDelta;
                        if (newWidth > 0.05)
                            zoneItem.visualWidth = newWidth * dividerHandle.drawingArea.width;
                    } else if (Math.abs(zoneLeft - startDividerPos) < 0.02) {
                        // Zone is to the RIGHT of divider - adjust x and width
                        var newX = startData.x + posDelta;
                        var newW = startData.width - posDelta;
                        if (newW > 0.05) {
                            zoneItem.visualX = newX * dividerHandle.drawingArea.width;
                            zoneItem.visualWidth = newW * dividerHandle.drawingArea.width;
                        }
                    }
                } else {
                    // Horizontal divider: adjust y/height
                    var zoneBottom = startData.y + startData.height;
                    var zoneTop = startData.y;
                    if (Math.abs(zoneBottom - startDividerPos) < 0.02) {
                        // Zone is ABOVE divider - adjust height
                        var newHeight = startData.height + posDelta;
                        if (newHeight > 0.05)
                            zoneItem.visualHeight = newHeight * dividerHandle.drawingArea.height;
                    } else if (Math.abs(zoneTop - startDividerPos) < 0.02) {
                        // Zone is BELOW divider - adjust y and height
                        var newY = startData.y + posDelta;
                        var newH = startData.height - posDelta;
                        if (newH > 0.05) {
                            zoneItem.visualY = newY * dividerHandle.drawingArea.height;
                            zoneItem.visualHeight = newH * dividerHandle.drawingArea.height;
                        }
                    }
                }
            }

            // Emit signal to parent
            dividerHandle.dragMoved(newPos);
        }

        onReleased: {
            if (!dividerHandle.isDragging)
                return;

            dividerHandle.isDragging = false;

            var finalPos = dividerHandle.dragPosition;
            var originalPos = dividerHandle.dividerPosition;

            // Emit signal to parent with final and original positions
            dividerHandle.dragEnded(finalPos, originalPos);
        }

        onCanceled: {
            if (dividerHandle.isDragging) {
                dividerHandle.isDragging = false;
                // Reset divider position to original
                dividerHandle.dragPosition = dividerHandle.dividerPosition;

                // Reset zones to original positions
                for (var j = 0; j < dividerHandle.zonesRepeater.count; j++) {
                    var zoneItem2 = dividerHandle.zonesRepeater.itemAt(j);
                    if (!zoneItem2)
                        continue;

                    var zoneId2 = zoneItem2.zoneId;
                    var startData2 = dividerHandle.zoneStartPositions[zoneId2];
                    if (!startData2)
                        continue;

                    zoneItem2.visualX = startData2.x * dividerHandle.drawingArea.width;
                    zoneItem2.visualY = startData2.y * dividerHandle.drawingArea.height;
                    zoneItem2.visualWidth = startData2.width * dividerHandle.drawingArea.width;
                    zoneItem2.visualHeight = startData2.height * dividerHandle.drawingArea.height;
                }

                // Emit signal to parent
                dividerHandle.dragCancelled();
            }
        }
    }

    // Public methods for parent to call
    function resetDragPosition() {
        dragPosition = dividerPosition;
    }

    function restoreZoneVisuals() {
        if (!drawingArea) return;

        for (var ri = 0; ri < zonesRepeater.count; ri++) {
            var zoneItemR = zonesRepeater.itemAt(ri);
            if (!zoneItemR)
                continue;

            var startDataR = zoneStartPositions[zoneItemR.zoneId];
            if (!startDataR)
                continue;

            zoneItemR.visualX = startDataR.x * drawingArea.width;
            zoneItemR.visualY = startDataR.y * drawingArea.height;
            zoneItemR.visualWidth = startDataR.width * drawingArea.width;
            zoneItemR.visualHeight = startDataR.height * drawingArea.height;
        }
    }
}
