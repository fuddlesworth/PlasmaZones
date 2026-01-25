// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Divider manager component for zone resizing
 *
 * Calculates and renders dividers between zones, allowing multi-zone resizing.
 * Handles divider dragging and zone geometry updates.
 */
Item {
    // Note: editorController is a required property, so it should always be set when component is created
    // If it's null, that means it wasn't passed correctly from parent
    // If failed, initRetryTimer will keep trying

    id: dividerManager

    property int dividerCount: 0 // Force Repeater to update
    property bool isDragging: false // Pause updates during divider drag
    required property var editorController
    required property real zoneSpacing
    required property Item drawingArea // Parent drawing area for dimensions (passed as reference)
    required property Repeater zonesRepeater // Zones repeater for accessing zone items (passed as reference)
    // Track the most recent divider positions that were actively resized.
    // Preserves exact positions after resize, preventing snapping to rounded edges.
    property var recentDividerPositions: ({
    })
    property var dividers: []
    // Track if we've done an initial update when drawing area becomes ready
    property bool initialUpdateDone: false

    // Update dividers when zones change
    function updateDividers() {
        var threshold = 0.01;

        // Round to threshold for grouping edges together
        // But keep track of exact positions to match against recent divider positions
        function roundToThreshold(val) {
            return Math.round(val / threshold) * threshold;
        }

        // Helper to find matching recent position for a divider based on affected zones
        function findRecentPositionForDivider(affectedZoneIds, isVertical) {
            var key = isVertical ? "v" : "h";
            if (!recentDividerPositions[key])
                return null;

            var recent = recentDividerPositions[key];
            // Create a key from sorted zone IDs to match how we stored it
            var dividerKey = affectedZoneIds.slice().sort().join(",");
            return recent[dividerKey] || null;
        }

        // This function assumes editorController is already checked - use tryUpdateDividers() instead
        if (!editorController) {
            dividerManager.dividerCount = 0;
            dividerManager.dividers = [];
            return ;
        }
        var newDividers = [];
        var zones = editorController.zones;
        if (!zones || zones.length < 2) {
            dividerManager.dividerCount = 0;
            dividerManager.dividers = [];
            return ;
        }
        // Don't calculate dividers until canvas has valid dimensions
        if (!drawingArea || drawingArea.width <= 0 || drawingArea.height <= 0) {
            dividerManager.dividerCount = 0;
            dividerManager.dividers = [];
            return ;
        }
        // Group edges by position to create continuous dividers
        // verticalEdges[x] = { leftZones: [...], rightZones: [...], minY, maxY, exactPosition: actual edge pos }
        // horizontalEdges[y] = { topZones: [...], bottomZones: [...], minX, maxX, exactPosition: actual edge pos }
        var verticalEdges = {
        };
        var horizontalEdges = {
        };
        // Collect all internal edges (not at 0 or 1)
        // Use exact positions, then round for grouping
        for (var i = 0; i < zones.length; i++) {
            var zone = zones[i];
            if (!zone || !zone.id)
                continue;

            var exactRightEdge = zone.x + zone.width;
            var exactBottomEdge = zone.y + zone.height;
            var rightEdge = roundToThreshold(exactRightEdge);
            var bottomEdge = roundToThreshold(exactBottomEdge);
            // Right edge (potential vertical divider) - skip if at canvas edge
            if (rightEdge > threshold && rightEdge < 1 - threshold) {
                if (!verticalEdges[rightEdge])
                    verticalEdges[rightEdge] = {
                    "leftZones": [],
                    "rightZones": [],
                    "minY": 1,
                    "maxY": 0,
                    "exactPosition": null
                };

                verticalEdges[rightEdge].leftZones.push(zone);
                verticalEdges[rightEdge].minY = Math.min(verticalEdges[rightEdge].minY, zone.y);
                verticalEdges[rightEdge].maxY = Math.max(verticalEdges[rightEdge].maxY, zone.y + zone.height);
                // Store exact position for this edge (will be used if no recent position found)
                if (verticalEdges[rightEdge].exactPosition === null)
                    verticalEdges[rightEdge].exactPosition = exactRightEdge;

            }
            // Left edge (zone is on right side of a divider)
            var exactLeftEdge = zone.x;
            var leftEdge = roundToThreshold(exactLeftEdge);
            if (leftEdge > threshold && leftEdge < 1 - threshold) {
                if (!verticalEdges[leftEdge])
                    verticalEdges[leftEdge] = {
                    "leftZones": [],
                    "rightZones": [],
                    "minY": 1,
                    "maxY": 0,
                    "exactPosition": null
                };

                verticalEdges[leftEdge].rightZones.push(zone);
                verticalEdges[leftEdge].minY = Math.min(verticalEdges[leftEdge].minY, zone.y);
                verticalEdges[leftEdge].maxY = Math.max(verticalEdges[leftEdge].maxY, zone.y + zone.height);
                // Store exact position for this edge (will be used if no recent position found)
                if (verticalEdges[leftEdge].exactPosition === null)
                    verticalEdges[leftEdge].exactPosition = exactLeftEdge;

            }
            // Bottom edge (potential horizontal divider) - skip if at canvas edge
            if (bottomEdge > threshold && bottomEdge < 1 - threshold) {
                if (!horizontalEdges[bottomEdge])
                    horizontalEdges[bottomEdge] = {
                    "topZones": [],
                    "bottomZones": [],
                    "minX": 1,
                    "maxX": 0,
                    "exactPosition": null
                };

                horizontalEdges[bottomEdge].topZones.push(zone);
                horizontalEdges[bottomEdge].minX = Math.min(horizontalEdges[bottomEdge].minX, zone.x);
                horizontalEdges[bottomEdge].maxX = Math.max(horizontalEdges[bottomEdge].maxX, zone.x + zone.width);
                // Store exact position for this edge (will be used if no recent position found)
                if (horizontalEdges[bottomEdge].exactPosition === null)
                    horizontalEdges[bottomEdge].exactPosition = exactBottomEdge;

            }
            // Top edge (zone is below a divider)
            var exactTopEdge = zone.y;
            var topEdge = roundToThreshold(exactTopEdge);
            if (topEdge > threshold && topEdge < 1 - threshold) {
                if (!horizontalEdges[topEdge])
                    horizontalEdges[topEdge] = {
                    "topZones": [],
                    "bottomZones": [],
                    "minX": 1,
                    "maxX": 0,
                    "exactPosition": null
                };

                horizontalEdges[topEdge].bottomZones.push(zone);
                horizontalEdges[topEdge].minX = Math.min(horizontalEdges[topEdge].minX, zone.x);
                horizontalEdges[topEdge].maxX = Math.max(horizontalEdges[topEdge].maxX, zone.x + zone.width);
                // Store exact position for this edge (will be used if no recent position found)
                if (horizontalEdges[topEdge].exactPosition === null)
                    horizontalEdges[topEdge].exactPosition = exactTopEdge;

            }
        }
        // Create vertical dividers (only where zones exist on BOTH sides)
        for (var xPos in verticalEdges) {
            // Recent position exists and is close enough - use it to preserve exact position
            // Use exact position from edge (matching recent position)
            // Use rounded position

            var edge = verticalEdges[xPos];
            if (edge.leftZones.length > 0 && edge.rightZones.length > 0) {
                var allZoneIds = [];
                var leftIds = [];
                var rightIds = [];
                for (var li = 0; li < edge.leftZones.length; li++) {
                    allZoneIds.push(edge.leftZones[li].id);
                    leftIds.push(edge.leftZones[li].id);
                }
                for (var ri = 0; ri < edge.rightZones.length; ri++) {
                    allZoneIds.push(edge.rightZones[ri].id);
                    rightIds.push(edge.rightZones[ri].id);
                }
                // Check if there's a recent position stored for these zones (from recent resize)
                var recentPos = findRecentPositionForDivider(allZoneIds, true);
                // Use recent position if available and close to the calculated position, otherwise use exact/rounded position
                var roundedPos = parseFloat(xPos);
                var dividerPos;
                if (recentPos !== null && Math.abs(recentPos - roundedPos) < 0.05)
                    dividerPos = recentPos;
                else if (edge.exactPosition !== null && edge.exactPosition !== undefined)
                    dividerPos = edge.exactPosition;
                else
                    dividerPos = roundedPos;
                newDividers.push({
                    "position": dividerPos,
                    "x": dividerPos * drawingArea.width,
                    "y": edge.minY * drawingArea.height,
                    "width": zoneSpacing,
                    "height": (edge.maxY - edge.minY) * drawingArea.height,
                    "isVertical": true,
                    "affectedZones": allZoneIds,
                    "leftZones": leftIds,
                    "rightZones": rightIds
                });
            }
        }
        // Create horizontal dividers (only where zones exist on BOTH sides)
        for (var yPos in horizontalEdges) {
            // Recent position exists and is close enough - use it to preserve exact position
            // Use exact position from edge (matching recent position)
            // Use rounded position

            var hEdge = horizontalEdges[yPos];
            if (hEdge.topZones.length > 0 && hEdge.bottomZones.length > 0) {
                var allHZoneIds = [];
                var topIds = [];
                var bottomIds = [];
                for (var ti = 0; ti < hEdge.topZones.length; ti++) {
                    allHZoneIds.push(hEdge.topZones[ti].id);
                    topIds.push(hEdge.topZones[ti].id);
                }
                for (var bi = 0; bi < hEdge.bottomZones.length; bi++) {
                    allHZoneIds.push(hEdge.bottomZones[bi].id);
                    bottomIds.push(hEdge.bottomZones[bi].id);
                }
                // Check if there's a recent position stored for these zones (from recent resize)
                var recentYPos = findRecentPositionForDivider(allHZoneIds, false);
                // Use recent position if available and close to the calculated position, otherwise use exact/rounded position
                var roundedYPos = parseFloat(yPos);
                var dividerYPos;
                if (recentYPos !== null && Math.abs(recentYPos - roundedYPos) < 0.05)
                    dividerYPos = recentYPos;
                else if (hEdge.exactPosition !== null && hEdge.exactPosition !== undefined)
                    dividerYPos = hEdge.exactPosition;
                else
                    dividerYPos = roundedYPos;
                newDividers.push({
                    "position": dividerYPos,
                    "x": hEdge.minX * drawingArea.width,
                    "y": dividerYPos * drawingArea.height,
                    "width": (hEdge.maxX - hEdge.minX) * drawingArea.width,
                    "height": zoneSpacing,
                    "isVertical": false,
                    "affectedZones": allHZoneIds,
                    "topZones": topIds,
                    "bottomZones": bottomIds
                });
            }
        }
        // Store calculated dividers in property (use id prefix for property assignment)
        dividerManager.dividers = newDividers;
        dividerManager.dividerCount = newDividers.length;
    }

    function scheduleUpdate() {
        if (!isDragging && !dividerUpdateTimer.running)
            dividerUpdateTimer.start();

    }

    // Helper function to safely update dividers, checking all prerequisites
    function tryUpdateDividers() {
        if (!editorController)
            return false;

        if (!drawingArea)
            return false;

        if (drawingArea.width <= 0 || drawingArea.height <= 0)
            return false;

        updateDividers();
        return true;
    }

    anchors.fill: parent
    z: 40 // Well below zones (z:60) to ensure zones and their resize handles receive mouse events first
    Component.onCompleted: {
        // Try immediate update
        Qt.callLater(function() {
            if (tryUpdateDividers())
                initialUpdateDone = true;

        });
    }

    // Debounce timer for smooth updates
    Timer {
        id: dividerUpdateTimer

        interval: 16 // ~60fps
        repeat: false
        onTriggered: {
            if (!isDragging)
                updateDividers();

        }
    }

    // Retry timer for initialization
    Timer {
        id: initRetryTimer

        interval: 100
        repeat: true
        running: !initialUpdateDone
        onTriggered: {
            if (tryUpdateDividers()) {
                initialUpdateDone = true;
                running = false;
            }
        }
    }

    // Watch for drawing area size changes - but only after editorController is set
    Connections {
        function onWidthChanged() {
            if (drawingArea && drawingArea.width > 0 && drawingArea.height > 0 && editorController) {
                if (!initialUpdateDone)
                    initialUpdateDone = tryUpdateDividers();
                else
                    scheduleUpdate();
            }
        }

        function onHeightChanged() {
            if (drawingArea && drawingArea.width > 0 && drawingArea.height > 0 && editorController) {
                if (!initialUpdateDone)
                    initialUpdateDone = tryUpdateDividers();
                else
                    scheduleUpdate();
            }
        }

        target: drawingArea
        enabled: drawingArea !== null && drawingArea !== undefined && editorController !== null && editorController !== undefined
    }

    // Connect to explicit editorController signals for more reliable updates
    Connections {
        function onZonesChanged() {
            // Zones changed - clear old divider positions (invalid for new zones)
            recentDividerPositions = {
            };
            // Update dividers immediately (not scheduled) to ensure they render
            if (!isDragging)
                tryUpdateDividers();

        }

        function onZoneGeometryChanged(changedZoneId) {
            scheduleUpdate();
        }

        target: editorController
        enabled: editorController !== null
    }

    // Find and render dividers between adjacent zones
    Repeater {
        id: dividerRepeater

        // Bind dividerData to dividers property - this ensures repeater updates when dividers change
        property var dividerData: dividerManager.dividers

        model: dividerManager.dividerCount

        Rectangle {
            // Vertical divider width = spacing
            // Vertical: span minus margins
            // Well below zones (z:60) and resize handles (z:200+)

            id: dividerHandle

            // Access divider info from the array - ensure index is valid
            property var dividerInfo: (index >= 0 && index < dividerRepeater.dividerData.length && dividerRepeater.dividerData[index]) ? dividerRepeater.dividerData[index] : null
            property real spacing: dividerManager.zoneSpacing
            property bool isVertical: dividerInfo ? dividerInfo.isVertical : false
            property real dividerPosition: dividerInfo ? dividerInfo.position : 0
            property var affectedZones: dividerInfo ? dividerInfo.affectedZones : []
            // Drag state for smooth visual updates
            property bool isDragging: false
            property real dragPosition: dividerPosition // Current position during drag
            property var zoneStartPositions: ({
            }) // Store initial zone positions
            // Use dragPosition if it differs significantly from dividerInfo.position
            // This handles the case where zones haven't updated yet after release
            // Threshold 0.02 matches roundToThreshold precision (0.01) with margin
            property bool shouldUseDragPosition: isDragging || (dividerInfo && Math.abs(dividerInfo.position - dragPosition) > 0.02)

            // Position divider in the gap between displayed zones
            // Zones are displayed with x+spacing/2, so divider must align with that
            // Use dragPosition if dragging or if dividerInfo hasn't caught up yet
            x: dividerInfo ? (dividerInfo.isVertical ? (shouldUseDragPosition ? dragPosition * dividerManager.drawingArea.width - spacing / 2 : dividerInfo.x - spacing / 2) : dividerInfo.x + spacing / 2) : 0
            y: dividerInfo ? (dividerInfo.isVertical ? dividerInfo.y + spacing / 2 : (shouldUseDragPosition ? dragPosition * dividerManager.drawingArea.height - spacing / 2 : dividerInfo.y - spacing / 2)) : 0
            width: dividerInfo ? (dividerInfo.isVertical ? spacing : dividerInfo.width - spacing) : 0 // Horizontal: span minus margins
            height: dividerInfo ? (dividerInfo.isVertical ? dividerInfo.height - spacing : spacing) : 0 // Horizontal divider height = spacing
            // Hide other dividers while one is being dragged to avoid phantom appearance
            // Ensure divider is visible if it has valid info and dimensions
            visible: {
                if (!dividerInfo || dividerInfo === null || dividerInfo === undefined)
                    return false;

                if (width <= 0 || height <= 0)
                    return false;

                if (!dividerManager.drawingArea || dividerManager.drawingArea.width <= 0 || dividerManager.drawingArea.height <= 0)
                    return false;

                if (dividerManager.isDragging && !isDragging)
                    return false;

                return true;
            }
            // Background - subtle base color, more visible on hover/drag
            color: (dividerMouseArea.containsMouse || isDragging) ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, isDragging ? 0.4 : 0.25) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08)
            border.color: (dividerMouseArea.containsMouse || isDragging) ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
            border.width: isDragging ? 2 : (dividerMouseArea.containsMouse ? 1 : 0)
            // Fully rounded ends (pill shape) - match zone border radius style
            // For vertical dividers: radius = half width, for horizontal: radius = half height
            radius: dividerHandle.isVertical ? (width / 2) : (height / 2)
            // Pill shape for vertical divider
            // Pill shape for horizontal divider
            // Dividers stay below zones (z:60) and well below resize handles (z:200+ within zones)
            // Only raise slightly when dragging to provide visual feedback
            // Lower z ensures resize handles can receive mouse events first
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
                // For most dividers, 5-7 dots looks good
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
                        // Distribute for vertical
                        // Center vertically for horizontal divider

                        required property int index
                        property bool isVertical: dividerHandle.isVertical
                        property int totalDots: gripPattern.dotCount
                        property real dotSize: gripPattern.dotSize
                        property real dotSpacing: gripPattern.dotSpacing

                        // Position dots along the divider, centered
                        x: isVertical ? (gripPattern.width / 2 - dotSize / 2) : (index * dotSpacing + (gripPattern.width - (totalDots - 1) * dotSpacing) / 2 - dotSize / 2)
                        // Center horizontally for vertical divider
                        // Distribute for horizontal
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
                // Position was clamped - zones won't be able to reach requested position
                // So we clamp the divider visual too
                // If zones moved, the operation was successful - dragPosition is already set correctly

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
                    dividerManager.isDragging = true; // Pause divider recalculation
                    // Now that we're dragging, prevent stealing to maintain the drag
                    preventStealing = true;
                    // Get initial mouse position in canvas coordinates
                    var mouseInCanvas = dividerMouseArea.mapToItem(dividerManager.drawingArea, mouse.x, mouse.y);
                    startMousePos = dividerHandle.isVertical ? mouseInCanvas.x : mouseInCanvas.y;
                    startDividerPos = dividerHandle.dividerPosition;
                    dividerHandle.dragPosition = startDividerPos;
                    // Capture starting positions of all affected zones
                    var zones = dividerManager.editorController.zones;
                    var startPos = {
                    };
                    for (var i = 0; i < zones.length; i++) {
                        var z = zones[i];
                        if (dividerHandle.affectedZones.indexOf(z.id) >= 0)
                            startPos[z.id] = {
                            "x": z.x,
                            "y": z.y,
                            "width": z.width,
                            "height": z.height
                        };

                    }
                    dividerHandle.zoneStartPositions = startPos;
                    // Set divider operation flag on all affected zones
                    // This prevents syncFromZoneData() from overwriting our visual updates
                    for (var j = 0; j < dividerManager.zonesRepeater.count; j++) {
                        var zoneItem = dividerManager.zonesRepeater.itemAt(j);
                        if (zoneItem && dividerHandle.affectedZones.indexOf(zoneItem.zoneId) >= 0)
                            zoneItem.isDividerOperation = true;

                    }
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !dividerHandle.isDragging)
                        return ;

                    var mouseInCanvas = dividerMouseArea.mapToItem(dividerManager.drawingArea, mouse.x, mouse.y);
                    var currentMousePos = dividerHandle.isVertical ? mouseInCanvas.x : mouseInCanvas.y;
                    var canvasSize = dividerHandle.isVertical ? dividerManager.drawingArea.width : dividerManager.drawingArea.height;
                    // Calculate new position
                    var delta = (currentMousePos - startMousePos) / canvasSize;
                    var requestedPos = startDividerPos + delta;
                    // Clamp to valid range (0.05 to 0.95) to prevent impossible positions
                    // This matches the minimum zone size constraint
                    var newPos = Math.max(0.05, Math.min(0.95, requestedPos));
                    var posDelta = newPos - startDividerPos;
                    // If position was clamped, update dragPosition to the clamped value
                    if (Math.abs(newPos - requestedPos) > 0.001)
                        dividerHandle.dragPosition = newPos;
                    else
                        dividerHandle.dragPosition = newPos;
                    // Directly update zone visuals (no C++ round-trip)
                    for (var i = 0; i < dividerManager.zonesRepeater.count; i++) {
                        var zoneItem = dividerManager.zonesRepeater.itemAt(i);
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
                                    zoneItem.visualWidth = newWidth * dividerManager.drawingArea.width;

                            } else if (Math.abs(zoneLeft - startDividerPos) < 0.02) {
                                // Zone is to the RIGHT of divider - adjust x and width
                                var newX = startData.x + posDelta;
                                var newW = startData.width - posDelta;
                                if (newW > 0.05) {
                                    zoneItem.visualX = newX * dividerManager.drawingArea.width;
                                    zoneItem.visualWidth = newW * dividerManager.drawingArea.width;
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
                                    zoneItem.visualHeight = newHeight * dividerManager.drawingArea.height;

                            } else if (Math.abs(zoneTop - startDividerPos) < 0.02) {
                                // Zone is BELOW divider - adjust y and height
                                var newY = startData.y + posDelta;
                                var newH = startData.height - posDelta;
                                if (newH > 0.05) {
                                    zoneItem.visualY = newY * dividerManager.drawingArea.height;
                                    zoneItem.visualHeight = newH * dividerManager.drawingArea.height;
                                }
                            }
                        }
                    }
                }
                onReleased: {
                    if (!dividerHandle.isDragging)
                        return ;

                    dividerHandle.isDragging = false;
                    dividerManager.isDragging = false;
                    // FIRST: Capture zone positions BEFORE the operation to compare later
                    var zonesBefore = {
                    };
                    var zonesData = dividerManager.editorController.zones;
                    for (var k = 0; k < zonesData.length; k++) {
                        var zone = zonesData[k];
                        if (dividerHandle.affectedZones.indexOf(zone.id) >= 0)
                            zonesBefore[zone.id] = {
                            "x": zone.x,
                            "y": zone.y,
                            "width": zone.width,
                            "height": zone.height
                        };

                    }
                    // Sync final state to C++ - BEFORE clearing isDividerOperation
                    // This prevents race condition where syncFromZoneData() resets visuals
                    var finalPos = dividerHandle.dragPosition;
                    var originalPos = dividerHandle.dividerPosition;
                    var info = dividerHandle.dividerInfo;
                    // Store the final position in recentDividerPositions so updateDividers() can preserve it
                    var key = dividerHandle.isVertical ? "v" : "h";
                    if (!dividerManager.recentDividerPositions[key])
                        dividerManager.recentDividerPositions[key] = {
                    };

                    // Store the position with a key based on the affected zones (to identify the same divider)
                    var dividerKey = dividerHandle.affectedZones.slice().sort().join(",");
                    dividerManager.recentDividerPositions[key][dividerKey] = finalPos;
                    if (dividerManager.editorController && info) {
                        if (dividerHandle.isVertical && info.leftZones && info.rightZones && info.leftZones.length > 0 && info.rightZones.length > 0) {
                            // For vertical divider: pick one zone from left, one from right
                            var leftZone = info.leftZones[0];
                            var rightZone = info.rightZones[0];
                            var leftId = (leftZone && leftZone.id !== undefined) ? leftZone.id : (typeof leftZone === 'string' ? leftZone : '');
                            var rightId = (rightZone && rightZone.id !== undefined) ? rightZone.id : (typeof rightZone === 'string' ? rightZone : '');
                            if (leftId && rightId)
                                dividerManager.editorController.resizeZonesAtDivider(leftId, rightId, finalPos, 0, true);

                        } else if (!dividerHandle.isVertical && info.topZones && info.bottomZones && info.topZones.length > 0 && info.bottomZones.length > 0) {
                            // For horizontal divider: pick one zone from top, one from bottom
                            var topZone = info.topZones[0];
                            var bottomZone = info.bottomZones[0];
                            var topId = (topZone && topZone.id !== undefined) ? topZone.id : (typeof topZone === 'string' ? topZone : '');
                            var bottomId = (bottomZone && bottomZone.id !== undefined) ? bottomZone.id : (typeof bottomZone === 'string' ? bottomZone : '');
                            if (topId && bottomId)
                                dividerManager.editorController.resizeZonesAtDivider(topId, bottomId, 0, finalPos, false);

                        }
                    }
                    // Clear divider operation flag after C++ update propagates
                    Qt.callLater(function() {
                        for (var i = 0; i < dividerManager.zonesRepeater.count; i++) {
                            var zoneItem = dividerManager.zonesRepeater.itemAt(i);
                            if (zoneItem && dividerHandle.affectedZones.indexOf(zoneItem.zoneId) >= 0)
                                zoneItem.isDividerOperation = false;

                        }
                    });
                    // Trigger divider recalculation
                    dividerManager.scheduleUpdate();
                    // Check if the resize was actually applied by checking if zones moved
                    // Wait for zones to update, then check if they actually moved
                    Qt.callLater(function() {
                        // Wait a bit more for C++ to update and signals to propagate
                        Qt.callLater(function() {
                            // Check if any affected zones actually moved from their original positions
                            var zonesMoved = false;
                            var zonesAfter = dividerManager.editorController.zones;
                            for (var i = 0; i < zonesAfter.length; i++) {
                                var zone = zonesAfter[i];
                                if (dividerHandle.affectedZones.indexOf(zone.id) >= 0) {
                                    var before = zonesBefore[zone.id];
                                    if (before) {
                                        // Check if zone position changed significantly (more than threshold)
                                        var xDiff = Math.abs(zone.x - before.x);
                                        var yDiff = Math.abs(zone.y - before.y);
                                        var wDiff = Math.abs(zone.width - before.width);
                                        var hDiff = Math.abs(zone.height - before.height);
                                        if (xDiff > 0.001 || yDiff > 0.001 || wDiff > 0.001 || hDiff > 0.001) {
                                            zonesMoved = true;
                                            break;
                                        }
                                    }
                                }
                            }
                            // If zones didn't move, the operation was rejected or never applied - reset
                            if (!zonesMoved) {
                                // Reset divider position
                                dividerHandle.dragPosition = originalPos;
                                // Clear the recent position entry so it doesn't try to preserve invalid position
                                if (dividerManager.recentDividerPositions[key] && dividerManager.recentDividerPositions[key][dividerKey])
                                    delete dividerManager.recentDividerPositions[key][dividerKey];

                                // Restore zone visuals from pre-drag state (model was not changed)
                                if (dividerManager.drawingArea) {
                                    for (var ri = 0; ri < dividerManager.zonesRepeater.count; ri++) {
                                        var zoneItemR = dividerManager.zonesRepeater.itemAt(ri);
                                        if (!zoneItemR)
                                            continue;

                                        var startDataR = dividerHandle.zoneStartPositions[zoneItemR.zoneId];
                                        if (!startDataR)
                                            continue;

                                        zoneItemR.visualX = startDataR.x * dividerManager.drawingArea.width;
                                        zoneItemR.visualY = startDataR.y * dividerManager.drawingArea.height;
                                        zoneItemR.visualWidth = startDataR.width * dividerManager.drawingArea.width;
                                        zoneItemR.visualHeight = startDataR.height * dividerManager.drawingArea.height;
                                    }
                                }
                                // Trigger update to recalculate divider position
                                dividerManager.scheduleUpdate();
                            }
                        });
                    });
                }
                onCanceled: {
                    if (dividerHandle.isDragging) {
                        // Clear divider operation flag on all affected zones
                        for (var i = 0; i < dividerManager.zonesRepeater.count; i++) {
                            var zoneItem = dividerManager.zonesRepeater.itemAt(i);
                            if (zoneItem && dividerHandle.affectedZones.indexOf(zoneItem.zoneId) >= 0)
                                zoneItem.isDividerOperation = false;

                        }
                        dividerHandle.isDragging = false;
                        dividerManager.isDragging = false;
                        // Reset divider position to original
                        dividerHandle.dragPosition = dividerHandle.dividerPosition;
                        // Reset zones to original positions
                        for (var j = 0; j < dividerManager.zonesRepeater.count; j++) {
                            var zoneItem2 = dividerManager.zonesRepeater.itemAt(j);
                            if (!zoneItem2)
                                continue;

                            var zoneId2 = zoneItem2.zoneId;
                            var startData2 = dividerHandle.zoneStartPositions[zoneId2];
                            if (!startData2)
                                continue;

                            zoneItem2.visualX = startData2.x * dividerManager.drawingArea.width;
                            zoneItem2.visualY = startData2.y * dividerManager.drawingArea.height;
                            zoneItem2.visualWidth = startData2.width * dividerManager.drawingArea.width;
                            zoneItem2.visualHeight = startData2.height * dividerManager.drawingArea.height;
                        }
                        dividerManager.scheduleUpdate();
                    }
                }
            }

        }

    }

}
