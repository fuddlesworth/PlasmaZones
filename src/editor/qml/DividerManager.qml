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

        // Bind dividerData to dividers property for repeater updates
        property var dividerData: dividerManager.dividers

        // Track the currently dragging handle for validation after release
        property var currentDraggingHandle: null
        property var zonesBefore: ({})

        model: dividerManager.dividerCount

        DividerHandle {
            id: dividerHandleItem

            // Required properties from DividerHandle component
            // Note: For required properties in Repeater delegate with integer model,
            // QML automatically binds 'index' from the model context
            dividerInfo: (index >= 0 && index < dividerRepeater.dividerData.length && dividerRepeater.dividerData[index]) ? dividerRepeater.dividerData[index] : null
            // index is auto-bound from Repeater context (required property)
            spacing: dividerManager.zoneSpacing
            drawingArea: dividerManager.drawingArea
            zonesRepeater: dividerManager.zonesRepeater

            // Hide other dividers while one is being dragged to avoid phantom appearance
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

            onDragStarted: function(zoneStartPositions) {
                dividerManager.isDragging = true;
                dividerRepeater.currentDraggingHandle = dividerHandleItem;

                // Set divider operation flag on all affected zones
                // This prevents syncFromZoneData() from overwriting our visual updates
                for (var j = 0; j < dividerManager.zonesRepeater.count; j++) {
                    var zoneItem = dividerManager.zonesRepeater.itemAt(j);
                    if (zoneItem && affectedZones.indexOf(zoneItem.zoneId) >= 0)
                        zoneItem.isDividerOperation = true;
                }
            }

            onDragMoved: function(newPosition) {
                // Visual updates are handled by DividerHandle component
            }

            onDragEnded: function(finalPosition, originalPosition) {
                dividerManager.isDragging = false;

                // Capture zone positions BEFORE the operation to compare later
                var zonesBefore = {};
                var zonesData = dividerManager.editorController.zones;
                for (var k = 0; k < zonesData.length; k++) {
                    var zone = zonesData[k];
                    if (affectedZones.indexOf(zone.id) >= 0)
                        zonesBefore[zone.id] = {
                            "x": zone.x,
                            "y": zone.y,
                            "width": zone.width,
                            "height": zone.height
                        };
                }
                dividerRepeater.zonesBefore = zonesBefore;

                // Store the final position in recentDividerPositions so updateDividers() can preserve it
                var key = isVertical ? "v" : "h";
                if (!dividerManager.recentDividerPositions[key])
                    dividerManager.recentDividerPositions[key] = {};

                var dividerKey = affectedZones.slice().sort().join(",");
                dividerManager.recentDividerPositions[key][dividerKey] = finalPosition;

                // Sync final state to C++
                var info = dividerInfo;
                if (dividerManager.editorController && info) {
                    if (isVertical && info.leftZones && info.rightZones && info.leftZones.length > 0 && info.rightZones.length > 0) {
                        var leftZone = info.leftZones[0];
                        var rightZone = info.rightZones[0];
                        var leftId = (leftZone && leftZone.id !== undefined) ? leftZone.id : (typeof leftZone === 'string' ? leftZone : '');
                        var rightId = (rightZone && rightZone.id !== undefined) ? rightZone.id : (typeof rightZone === 'string' ? rightZone : '');
                        if (leftId && rightId)
                            dividerManager.editorController.resizeZonesAtDivider(leftId, rightId, finalPosition, 0, true);
                    } else if (!isVertical && info.topZones && info.bottomZones && info.topZones.length > 0 && info.bottomZones.length > 0) {
                        var topZone = info.topZones[0];
                        var bottomZone = info.bottomZones[0];
                        var topId = (topZone && topZone.id !== undefined) ? topZone.id : (typeof topZone === 'string' ? topZone : '');
                        var bottomId = (bottomZone && bottomZone.id !== undefined) ? bottomZone.id : (typeof bottomZone === 'string' ? bottomZone : '');
                        if (topId && bottomId)
                            dividerManager.editorController.resizeZonesAtDivider(topId, bottomId, 0, finalPosition, false);
                    }
                }

                // Clear divider operation flag after C++ update propagates
                var capturedAffectedZones = affectedZones.slice();
                Qt.callLater(function() {
                    for (var i = 0; i < dividerManager.zonesRepeater.count; i++) {
                        var zoneItem = dividerManager.zonesRepeater.itemAt(i);
                        if (zoneItem && capturedAffectedZones.indexOf(zoneItem.zoneId) >= 0)
                            zoneItem.isDividerOperation = false;
                    }
                });

                // Trigger divider recalculation
                dividerManager.scheduleUpdate();

                // Validate that the resize was actually applied
                var capturedHandle = dividerHandleItem;
                var capturedOriginalPos = originalPosition;
                var capturedKey = key;
                var capturedDividerKey = dividerKey;
                Qt.callLater(function() {
                    Qt.callLater(function() {
                        var zonesMoved = false;
                        var zonesAfter = dividerManager.editorController.zones;
                        var capturedZonesBefore = dividerRepeater.zonesBefore;
                        for (var i = 0; i < zonesAfter.length; i++) {
                            var zone = zonesAfter[i];
                            if (capturedAffectedZones.indexOf(zone.id) >= 0) {
                                var before = capturedZonesBefore[zone.id];
                                if (before) {
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
                        if (!zonesMoved && capturedHandle) {
                            capturedHandle.resetDragPosition();
                            if (dividerManager.recentDividerPositions[capturedKey] && dividerManager.recentDividerPositions[capturedKey][capturedDividerKey])
                                delete dividerManager.recentDividerPositions[capturedKey][capturedDividerKey];
                            capturedHandle.restoreZoneVisuals();
                            dividerManager.scheduleUpdate();
                        }
                    });
                });
            }

            onDragCancelled: {
                dividerManager.isDragging = false;

                // Clear divider operation flag on all affected zones
                for (var i = 0; i < dividerManager.zonesRepeater.count; i++) {
                    var zoneItem = dividerManager.zonesRepeater.itemAt(i);
                    if (zoneItem && affectedZones.indexOf(zoneItem.zoneId) >= 0)
                        zoneItem.isDividerOperation = false;
                }

                dividerManager.scheduleUpdate();
            }
        }
    }

}
