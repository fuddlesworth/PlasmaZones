// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Helper component for complex zone operations
 *
 * Provides reusable functions for zone manipulation that require
 * complex logic or animation coordination.
 * Extracted from EditorWindow.qml to reduce file size.
 */
QtObject {
    id: zoneOperations

    /**
     * @brief Delete zone with fill animation for adjacent zones
     * @param zoneIdToDelete The ID of zone to delete
     * @param controller The EditorController instance
     * @param zonesRepeater The zones Repeater instance
     * @param canvasWidth Canvas width for coordinate conversion
     * @param canvasHeight Canvas height for coordinate conversion
     */
    function deleteWithFillAnimation(zoneIdToDelete, controller, zonesRepeater, canvasWidth, canvasHeight) {
        if (!controller || !zoneIdToDelete)
            return ;

        // Find adjacent zones and store their current geometry BEFORE delete
        var adjacentZones = controller.findAdjacentZones(zoneIdToDelete);
        var adjacentIds = [];
        var oldGeometries = {
        };
        // Collect all adjacent zone IDs and current geometry
        var directions = ["left", "right", "top", "bottom"];
        for (var d = 0; d < directions.length; d++) {
            var dir = directions[d];
            if (adjacentZones[dir]) {
                var adjList = adjacentZones[dir];
                for (var i = 0; i < adjList.length; i++) {
                    var adjId = adjList[i];
                    if (adjacentIds.indexOf(adjId) === -1) {
                        // Find the zone item to get current geometry
                        var zoneItem = findZoneItemById(adjId, zonesRepeater);
                        if (zoneItem) {
                            adjacentIds.push(adjId);
                            oldGeometries[adjId] = {
                                "x": zoneItem.visualX,
                                "y": zoneItem.visualY,
                                "width": zoneItem.visualWidth,
                                "height": zoneItem.visualHeight
                            };
                            // Set isAnimatingFill BEFORE delete to block geometry updates
                            zoneItem.isAnimatingFill = true;
                        }
                    }
                }
            }
        }
        // Delete the zone (C++ will expand neighbors)
        controller.deleteZoneWithFill(zoneIdToDelete, true);
        // Animate the adjacent zones - use Qt.callLater to ensure model is updated
        if (adjacentIds.length > 0)
            Qt.callLater(function() {
            animateAdjacentZones(adjacentIds, oldGeometries, controller, zonesRepeater, canvasWidth, canvasHeight);
        });

    }

    /**
     * @brief Find zone item by ID from Repeater
     */
    function findZoneItemById(zoneId, zonesRepeater) {
        if (!zoneId || !zonesRepeater)
            return null;

        for (var j = 0; j < zonesRepeater.count; j++) {
            var candidate = zonesRepeater.itemAt(j);
            if (candidate && candidate.zoneId === zoneId)
                return candidate;

        }
        return null;
    }

    /**
     * @brief Animate adjacent zones after delete
     */
    function animateAdjacentZones(adjacentIds, oldGeometries, controller, zonesRepeater, canvasW, canvasH) {
        for (var k = 0; k < adjacentIds.length; k++) {
            var targetId = adjacentIds[k];
            var oldGeom = oldGeometries[targetId];
            if (!oldGeom)
                continue;

            // Find the item in repeater by ID (may have been recreated)
            var item = findZoneItemById(targetId, zonesRepeater);
            if (!item)
                continue;

            // Get new geometry from model
            var zones = controller.zones;
            var foundZone = null;
            for (var n = 0; n < zones.length; n++) {
                if (zones[n].id === targetId) {
                    foundZone = zones[n];
                    break;
                }
            }
            if (!foundZone) {
                if (item)
                    item.isAnimatingFill = false;

                continue;
            }
            var newX = foundZone.x * canvasW;
            var newY = foundZone.y * canvasH;
            var newW = foundZone.width * canvasW;
            var newH = foundZone.height * canvasH;
            // Only animate if geometry changed significantly
            if (Math.abs(newX - oldGeom.x) > 1 || Math.abs(newY - oldGeom.y) > 1 || Math.abs(newW - oldGeom.width) > 1 || Math.abs(newH - oldGeom.height) > 1) {
                // Ensure visual is at old values (in case item was recreated)
                item.isAnimatingFill = true;
                item.visualX = oldGeom.x;
                item.visualY = oldGeom.y;
                item.visualWidth = oldGeom.width;
                item.visualHeight = oldGeom.height;
                // Start animation to new geometry
                item.startFillAnimation(newX, newY, newW, newH);
            } else {
                item.isAnimatingFill = false;
            }
        }
    }

}
