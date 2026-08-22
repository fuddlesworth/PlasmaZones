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
     */
    function deleteWithFillAnimation(zoneIdToDelete, controller, zonesRepeater) {
        if (!controller || !zoneIdToDelete)
            return;

        // Find adjacent zones and store their current geometry BEFORE delete
        var adjacentZones = controller.findAdjacentZones(zoneIdToDelete);
        var adjacentIds = [];
        var oldGeometries = {};
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
            Qt.callLater(function () {
                animateAdjacentZones(adjacentIds, oldGeometries, controller, zonesRepeater);
            });
    }

    /**
     * @brief Split a zone, animating the original as it gives up its half
     * @param zoneIdToSplit The ID of the zone to split
     * @param horizontal True to split horizontally, false for vertically
     * @param controller The EditorController instance
     * @param zonesRepeater The zones Repeater instance
     *
     * The mirror of deleteWithFillAnimation. ZoneManager::splitZone shrinks the
     * ORIGINAL zone in place (same id, one dimension halved) and creates a new
     * zone for the other half, so the original is a zone giving space up — the
     * `editor.snapOut` leg, which the fill animator selects by direction.
     * Without this the original jumped to half size in one frame while the
     * delete path, which is the same operation in reverse, animated.
     *
     * Only the original is animated. The new half has no previous geometry to
     * travel from, so animating it would mean inventing a start rect.
     */
    function splitWithShrinkAnimation(zoneIdToSplit, horizontal, controller, zonesRepeater) {
        if (!controller || !zoneIdToSplit) {
            return;
        }

        var item = findZoneItemById(zoneIdToSplit, zonesRepeater);
        if (!item) {
            // No item to animate (the repeater may not have realised it yet).
            // The split itself must still happen.
            controller.splitZone(zoneIdToSplit, horizontal);
            return;
        }

        // Abort any fill still in flight BEFORE reading the geometry, the same
        // reason ZoneDragHandler does it: a running fill writes visualX/Y/W/H
        // every frame, so capturing mid-flight would start the shrink from an
        // interpolated rect rather than from the zone as the user sees it.
        item.stopFillAnimation();

        var oldGeometries = {};
        oldGeometries[zoneIdToSplit] = {
            "x": item.visualX,
            "y": item.visualY,
            "width": item.visualWidth,
            "height": item.visualHeight
        };
        // Latch this item for the case where the delegate SURVIVES the split.
        // It usually does not: `zones` is a QVariantList, so the two
        // zonesChanged emissions inside splitZone regenerate the Repeater's
        // delegates and destroy this one. animateAdjacentZones re-finds the
        // (possibly new) item and re-latches it before touching geometry, which
        // is what actually covers the regenerate case.
        item.isAnimatingFill = true;

        var newZoneId = controller.splitZone(zoneIdToSplit, horizontal);
        if (!newZoneId) {
            // Refused (the halves would be under the minimum size). Nothing
            // moved, so release the latch rather than leaving the zone frozen.
            item.isAnimatingFill = false;
            return;
        }

        // Restore SYNCHRONOUSLY, not through Qt.callLater.
        //
        // splitZone writes the model and emits zonesChanged inside the call, so
        // by the time it returns the Repeater has already rebuilt this zone's
        // delegate at the halved geometry. Deferring the restore lets a frame
        // paint in between, and it is visible: the zone snaps to half size, then
        // jumps back to full and animates down again. Doing it here keeps the
        // whole sequence inside one JS call stack, so nothing paints until the
        // visual is back at the pre-split rect with the animation already armed.
        //
        // The delete path can afford the deferral because its adjacent zones are
        // GROWING into space that was already blank — a frame at the new size
        // reads as the delete having landed, not as a flicker.
        var fresh = findZoneItemById(zoneIdToSplit, zonesRepeater);
        if (!fresh) {
            // The delegate has not been rebuilt yet. Fall back to the deferred
            // path rather than dropping the animation entirely.
            Qt.callLater(function () {
                animateAdjacentZones([zoneIdToSplit], oldGeometries, controller, zonesRepeater);
            });
            return;
        }

        // The rebuilt delegate already carries the post-split geometry (its
        // Component.onCompleted syncs from the model), so it is the target —
        // no second model lookup, and no dependence on the two geometry modes
        // agreeing about units.
        var target = {
            "x": fresh.visualX,
            "y": fresh.visualY,
            "width": fresh.visualWidth,
            "height": fresh.visualHeight
        };
        var oldGeom = oldGeometries[zoneIdToSplit];
        // Geometry unchanged means this is still the PRE-split delegate: the
        // rebuild has not run yet, so there is nothing to animate from here.
        // Defer rather than give up, which lands on the old behaviour (a frame
        // at the new size, then the animation) instead of losing it entirely.
        if (Math.abs(target.x - oldGeom.x) <= 1 && Math.abs(target.y - oldGeom.y) <= 1 && Math.abs(target.width - oldGeom.width) <= 1 && Math.abs(target.height - oldGeom.height) <= 1) {
            Qt.callLater(function () {
                animateAdjacentZones([zoneIdToSplit], oldGeometries, controller, zonesRepeater);
            });
            return;
        }

        fresh.isAnimatingFill = true;
        fresh.visualX = oldGeom.x;
        fresh.visualY = oldGeom.y;
        fresh.visualWidth = oldGeom.width;
        fresh.visualHeight = oldGeom.height;
        fresh.startFillAnimation(target.x, target.y, target.width, target.height);
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
    function animateAdjacentZones(adjacentIds, oldGeometries, controller, zonesRepeater) {
        for (var k = 0; k < adjacentIds.length; k++) {
            var targetId = adjacentIds[k];
            var oldGeom = oldGeometries[targetId];
            if (!oldGeom)
                continue;

            // Find the item in repeater by ID (may have been recreated)
            var item = findZoneItemById(targetId, zonesRepeater);
            // A miss cannot strand a latched item: this IS the repeater lookup,
            // so "alive but not findable" is not a reachable state. Either the
            // delegate survived and is found here, or it was destroyed and took
            // the latch with it.
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
            // The zone map's x/y/width/height are relative (0-1) in BOTH
            // geometry modes: ZoneManager keeps them synced from the fixed
            // pixel values (fixedX/fixedY/fixedWidth/fixedHeight), which is
            // where pixels live. Scale by canvas size directly; the item's
            // mode-aware converters would divide by screen size a second
            // time for fixed zones and collapse the target to ~0.
            var newX = foundZone.x * item.canvasWidth;
            var newY = foundZone.y * item.canvasHeight;
            var newW = foundZone.width * item.canvasWidth;
            var newH = foundZone.height * item.canvasHeight;
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
