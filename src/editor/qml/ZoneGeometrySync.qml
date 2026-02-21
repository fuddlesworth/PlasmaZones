// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Geometry synchronization handler for zones
 *
 * Handles syncing visual properties from model data and C++ signals.
 * Extracted from EditorZone.qml to reduce file size.
 */
Item {
    // 0 = Idle

    id: syncHandler

    // Required references
    required property Item zoneRoot
    required property var controller

    // Sync visual properties from model data
    function syncFromZoneData() {
        // Guard: zone removed from Repeater - avoid use-after-free from pending Qt.callLater
        if (!zoneRoot || !zoneRoot.parent)
            return;
        // Don't sync during active operations
        if (zoneRoot.operationState !== 0 || zoneRoot.isDividerOperation)
            return ;

        if (!zoneRoot.zoneData)
            return ;

        if (zoneRoot.canvasWidth > 0 && zoneRoot.canvasHeight > 0 && isFinite(zoneRoot.canvasWidth) && isFinite(zoneRoot.canvasHeight)) {
            var x = 0, y = 0, w = 0.25, h = 0.25;
            var zd = zoneRoot.zoneData;
            var isFixed = zd.geometryMode === 1;

            if (isFixed) {
                // Fixed mode: read pixel coords from fixedX/fixedY/fixedWidth/fixedHeight
                x = (zd.fixedX !== undefined && zd.fixedX !== null && isFinite(zd.fixedX)) ? zd.fixedX : 0;
                y = (zd.fixedY !== undefined && zd.fixedY !== null && isFinite(zd.fixedY)) ? zd.fixedY : 0;
                w = (zd.fixedWidth !== undefined && zd.fixedWidth !== null && isFinite(zd.fixedWidth) && zd.fixedWidth > 0) ? zd.fixedWidth : 100;
                h = (zd.fixedHeight !== undefined && zd.fixedHeight !== null && isFinite(zd.fixedHeight) && zd.fixedHeight > 0) ? zd.fixedHeight : 100;

                // Validate
                if (!isFinite(x) || isNaN(x)) x = 0;
                if (!isFinite(y) || isNaN(y)) y = 0;
                if (!isFinite(w) || isNaN(w) || w <= 0) w = 100;
                if (!isFinite(h) || isNaN(h) || h <= 0) h = 100;

                // Clamp position >= 0
                x = Math.max(0, x);
                y = Math.max(0, y);

                // Convert pixel coords to canvas coords using screen dimensions
                var sw = zoneRoot.screenWidth > 0 ? zoneRoot.screenWidth : 1920;
                var sh = zoneRoot.screenHeight > 0 ? zoneRoot.screenHeight : 1080;
                var newVisualX = (x / sw) * zoneRoot.canvasWidth;
                var newVisualY = (y / sh) * zoneRoot.canvasHeight;
                var newVisualWidth = (w / sw) * zoneRoot.canvasWidth;
                var newVisualHeight = (h / sh) * zoneRoot.canvasHeight;
            } else {
                // Relative mode: read 0-1 normalized coords
                if (zd.x !== undefined && zd.x !== null && isFinite(zd.x))
                    x = zd.x;
                else if (zd.relativeGeometry && zd.relativeGeometry.x !== undefined)
                    x = zd.relativeGeometry.x || 0;
                if (zd.y !== undefined && zd.y !== null && isFinite(zd.y))
                    y = zd.y;
                else if (zd.relativeGeometry && zd.relativeGeometry.y !== undefined)
                    y = zd.relativeGeometry.y || 0;
                if (zd.width !== undefined && zd.width !== null && isFinite(zd.width) && zd.width > 0)
                    w = zd.width;
                else if (zd.relativeGeometry && zd.relativeGeometry.width !== undefined)
                    w = zd.relativeGeometry.width || 0.25;
                if (zd.height !== undefined && zd.height !== null && isFinite(zd.height) && zd.height > 0)
                    h = zd.height;
                else if (zd.relativeGeometry && zd.relativeGeometry.height !== undefined)
                    h = zd.relativeGeometry.height || 0.25;
                // Ensure valid values
                if (!isFinite(x) || isNaN(x))
                    x = 0;

                if (!isFinite(y) || isNaN(y))
                    y = 0;

                if (!isFinite(w) || isNaN(w) || w <= 0)
                    w = 0.25;

                if (!isFinite(h) || isNaN(h) || h <= 0)
                    h = 0.25;

                // Clamp to valid range
                x = Math.max(0, Math.min(1, x));
                y = Math.max(0, Math.min(1, y));
                w = Math.max(0.05, Math.min(1, w));
                h = Math.max(0.05, Math.min(1, h));
                var newVisualX = x * zoneRoot.canvasWidth;
                var newVisualY = y * zoneRoot.canvasHeight;
                var newVisualWidth = w * zoneRoot.canvasWidth;
                var newVisualHeight = h * zoneRoot.canvasHeight;
            }

            if (zoneRoot.visualWidth === 0 || zoneRoot.visualHeight === 0 || !isFinite(zoneRoot.visualWidth) || !isFinite(zoneRoot.visualHeight)) {
                zoneRoot.visualX = newVisualX;
                zoneRoot.visualY = newVisualY;
                zoneRoot.visualWidth = newVisualWidth;
                zoneRoot.visualHeight = newVisualHeight;
            } else if (zoneRoot.operationState === 0 && !zoneRoot.isDividerOperation) {
                zoneRoot.visualX = newVisualX;
                zoneRoot.visualY = newVisualY;
                zoneRoot.visualWidth = newVisualWidth;
                zoneRoot.visualHeight = newVisualHeight;
            }
        }
    }

    // Initialize dimensions when canvas becomes valid
    function ensureDimensionsInitialized() {
        // Guard: zone removed from Repeater - avoid use-after-free from pending Qt.callLater
        if (!zoneRoot || !zoneRoot.parent)
            return;
        if (zoneRoot.canvasWidth > 0 && zoneRoot.canvasHeight > 0 && isFinite(zoneRoot.canvasWidth) && isFinite(zoneRoot.canvasHeight)) {
            syncFromZoneData();
            if ((zoneRoot.visualWidth === 0 || zoneRoot.visualHeight === 0 || !isFinite(zoneRoot.visualWidth) || !isFinite(zoneRoot.visualHeight)) && zoneRoot.zoneData) {
                var zd = zoneRoot.zoneData;
                var x = (zd.x !== undefined && zd.x !== null && isFinite(zd.x)) ? zd.x : 0;
                var y = (zd.y !== undefined && zd.y !== null && isFinite(zd.y)) ? zd.y : 0;
                var w = (zd.width !== undefined && zd.width !== null && isFinite(zd.width) && zd.width > 0) ? zd.width : 0.25;
                var h = (zd.height !== undefined && zd.height !== null && isFinite(zd.height) && zd.height > 0) ? zd.height : 0.25;
                if (zoneRoot.visualWidth === 0 || !isFinite(zoneRoot.visualWidth))
                    zoneRoot.visualWidth = Math.max(0.05, w) * zoneRoot.canvasWidth;

                if (zoneRoot.visualHeight === 0 || !isFinite(zoneRoot.visualHeight))
                    zoneRoot.visualHeight = Math.max(0.05, h) * zoneRoot.canvasHeight;

                if (zoneRoot.visualX === 0 || !isFinite(zoneRoot.visualX))
                    zoneRoot.visualX = x * zoneRoot.canvasWidth;

                if (zoneRoot.visualY === 0 || !isFinite(zoneRoot.visualY))
                    zoneRoot.visualY = y * zoneRoot.canvasHeight;

            }
        }
    }

    // Handle geometry changes from C++
    Connections {
        // Avoid "QQmlVMEMetaObject: attempted to evaluate a function in an invalid context"
        // when zoneRoot or controllerRef are torn down during undo/redo.

        function onZoneGeometryChanged(changedZoneId) {
            if (changedZoneId !== zoneRoot.zoneId || !controller)
                return ;

            if ((zoneRoot.operationState === 1 || zoneRoot.operationState === 2) && !zoneRoot.isDividerOperation)
                return ;

            if (zoneRoot.isDividerOperation)
                return ;

            if (zoneRoot.isAnimatingFill)
                return ;

            var zoneIdRef = zoneRoot.zoneId;
            var canvasW = zoneRoot.canvasWidth;
            var canvasH = zoneRoot.canvasHeight;
            var controllerRef = controller;
            Qt.callLater(function() {
                try {
                    if (!zoneRoot || !controllerRef)
                        return ;

                    if (zoneRoot.isAnimatingFill)
                        return ;

                    // Guard: if zoneRoot was removed from the scene (e.g. during Repeater update),
                    // parent may be null; avoid accessing further to prevent "invalid context" crash.
                    if (!zoneRoot.parent)
                        return ;

                    var zones = controllerRef ? controllerRef.zones : null;
                    if (!zones || zones.length === 0)
                        return ;

                    var updatedZone = null;
                    for (var i = 0; i < zones.length; i++) {
                        var zone = zones[i];
                        if (!zone || !zone.id)
                            continue;

                        if (zone.id === zoneIdRef || zone.id.toString() === zoneIdRef.toString()) {
                            updatedZone = zone;
                            break;
                        }
                    }
                    if (!updatedZone)
                        return ;

                    if (canvasW <= 0 || canvasH <= 0 || !isFinite(canvasW) || !isFinite(canvasH))
                        return ;

                    var isFixed = updatedZone.geometryMode === 1;
                    var newVisualX, newVisualY, newVisualW, newVisualH;

                    if (isFixed) {
                        // Fixed mode: read pixel coords
                        var fx = (updatedZone.fixedX !== undefined && updatedZone.fixedX !== null) ? updatedZone.fixedX : 0;
                        var fy = (updatedZone.fixedY !== undefined && updatedZone.fixedY !== null) ? updatedZone.fixedY : 0;
                        var fw = (updatedZone.fixedWidth !== undefined && updatedZone.fixedWidth !== null && updatedZone.fixedWidth > 0) ? updatedZone.fixedWidth : 100;
                        var fh = (updatedZone.fixedHeight !== undefined && updatedZone.fixedHeight !== null && updatedZone.fixedHeight > 0) ? updatedZone.fixedHeight : 100;
                        var sw = zoneRoot.screenWidth > 0 ? zoneRoot.screenWidth : 1920;
                        var sh = zoneRoot.screenHeight > 0 ? zoneRoot.screenHeight : 1080;
                        newVisualX = (fx / sw) * canvasW;
                        newVisualY = (fy / sh) * canvasH;
                        newVisualW = (fw / sw) * canvasW;
                        newVisualH = (fh / sh) * canvasH;
                    } else {
                        // Relative mode: read 0-1 coords
                        var x = (updatedZone.x !== undefined && updatedZone.x !== null) ? updatedZone.x : 0;
                        var y = (updatedZone.y !== undefined && updatedZone.y !== null) ? updatedZone.y : 0;
                        var w = (updatedZone.width !== undefined && updatedZone.width !== null && updatedZone.width > 0) ? updatedZone.width : 0.25;
                        var h = (updatedZone.height !== undefined && updatedZone.height !== null && updatedZone.height > 0) ? updatedZone.height : 0.25;
                        if (!isFinite(x) || isNaN(x)) x = 0;
                        if (!isFinite(y) || isNaN(y)) y = 0;
                        if (!isFinite(w) || isNaN(w) || w <= 0) w = 0.25;
                        if (!isFinite(h) || isNaN(h) || h <= 0) h = 0.25;
                        newVisualX = isFinite(x) ? x * canvasW : 0;
                        newVisualY = isFinite(y) ? y * canvasH : 0;
                        newVisualW = (isFinite(w) && w > 0) ? w * canvasW : canvasW * 0.25;
                        newVisualH = (isFinite(h) && h > 0) ? h * canvasH : canvasH * 0.25;
                    }
                    if (!zoneRoot || zoneRoot.visualX === undefined)
                        return ;

                    var threshold = 1;
                    var xDiff = Math.abs(zoneRoot.visualX - newVisualX);
                    var yDiff = Math.abs(zoneRoot.visualY - newVisualY);
                    var wDiff = Math.abs(zoneRoot.visualWidth - newVisualW);
                    var hDiff = Math.abs(zoneRoot.visualHeight - newVisualH);
                    if (xDiff < threshold && yDiff < threshold && wDiff < threshold && hDiff < threshold)
                        return ;

                    zoneRoot.visualX = newVisualX;
                    zoneRoot.visualY = newVisualY;
                    zoneRoot.visualWidth = newVisualW;
                    zoneRoot.visualHeight = newVisualH;
                } catch (e) {
                }
            });
        }

        target: controller
    }

}
