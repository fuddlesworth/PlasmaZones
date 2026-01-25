// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Mouse handler for zone drag-to-move operations
 *
 * Handles zone dragging with snapping and fill preview.
 * Extracted from EditorZone.qml to reduce file size.
 */
MouseArea {
    // Corners
    // Edges

    id: dragHandler

    // Required references to parent zone
    required property Item zoneRoot
    required property var controller
    required property var snapIndicator
    // Internal state
    property point dragStart: Qt.point(0, 0)
    property point mouseStart: Qt.point(0, 0)
    property size originalSize: Qt.size(0, 0)
    property bool fillPreviewActive: false
    property var fillRegion: null
    property var committedFillRegion: null // Preserve fill region even after modifier key released
    property bool hasDragged: false // Track if actual drag movement occurred

    anchors.fill: parent
    hoverEnabled: true
    acceptedButtons: Qt.LeftButton | Qt.RightButton
    z: 1 // Below handles and buttons
    focus: false
    onClicked: function(mouse) {
        // Don't handle clicks if over buttons or handles
        if (zoneRoot.anyButtonHovered || zoneRoot.anyHandleHovered)
            return ;

        if (mouse.button === Qt.RightButton)
            zoneRoot.contextMenuRequested();
        else
            zoneRoot.clicked(mouse); // Pass mouse event for modifier key handling
        // Ensure parent drawingArea maintains focus for keyboard navigation
        var parentItem = zoneRoot.parent;
        while (parentItem) {
            if (parentItem.objectName === "drawingArea" || parentItem.id === "drawingArea") {
                Qt.callLater(function() {
                    if (parentItem)
                        parentItem.forceActiveFocus();

                });
                break;
            }
            parentItem = parentItem.parent;
        }
    }
    onPressed: function(mouse) {
        // Check hover state first
        if (zoneRoot.anyButtonHovered || zoneRoot.anyHandleHovered) {
            mouse.accepted = false;
            return ;
        }
        var mx = mouse.x, my = mouse.y;
        var zw = zoneRoot.width, zh = zoneRoot.height;
        // Geometric check: detect if click is near a corner/edge handle
        var handleHitSize = 24;
        var edgeHandleHalfSize = zoneRoot.handleSize * 3;
        var nearCornerOrEdge = ((mx < handleHitSize && my < handleHitSize) || (mx > zw - handleHitSize && my < handleHitSize) || (mx < handleHitSize && my > zh - handleHitSize) || (mx > zw - handleHitSize && my > zh - handleHitSize) || (mx > zw / 2 - edgeHandleHalfSize && mx < zw / 2 + edgeHandleHalfSize && my < handleHitSize) || (mx > zw / 2 - edgeHandleHalfSize && mx < zw / 2 + edgeHandleHalfSize && my > zh - handleHitSize) || (my > zh / 2 - edgeHandleHalfSize && my < zh / 2 + edgeHandleHalfSize && mx < handleHitSize) || (my > zh / 2 - edgeHandleHalfSize && my < zh / 2 + edgeHandleHalfSize && mx > zw - handleHitSize));
        if (nearCornerOrEdge) {
            mouse.accepted = false;
            return ;
        }
        if (mouse.button === Qt.LeftButton && zoneRoot.operationState === 0) {
            // EditorZone.State.Idle = 0
            mouse.accepted = true;
            zoneRoot.operationState = 1; // EditorZone.State.Dragging = 1
            dragStart = Qt.point(zoneRoot.visualX, zoneRoot.visualY);
            originalSize = Qt.size(zoneRoot.visualWidth, zoneRoot.visualHeight);
            animateOffTimer.stop();
            fillPreviewActive = false;
            fillRegion = null;
            committedFillRegion = null;
            hasDragged = false; // Reset drag tracking
            zoneRoot.animateFillPreview = false;
            var canvasItem = zoneRoot.parent;
            var mouseInCanvas = dragHandler.mapToItem(canvasItem, mouse.x, mouse.y);
            mouseStart = Qt.point(mouseInCanvas.x, mouseInCanvas.y);
            zoneRoot.operationStarted(zoneRoot.zoneId, zoneRoot.visualX, zoneRoot.visualY, zoneRoot.visualWidth, zoneRoot.visualHeight);
            // Start multi-zone drag if this zone is part of a multi-selection
            if (controller && zoneRoot.canvasWidth > 0 && zoneRoot.canvasHeight > 0) {
                var relX = zoneRoot.visualX / zoneRoot.canvasWidth;
                var relY = zoneRoot.visualY / zoneRoot.canvasHeight;
                controller.startMultiZoneDrag(zoneRoot.zoneId, relX, relY);
            }
        }
    }
    onPositionChanged: function(mouse) {
        if (pressed && zoneRoot.operationState === 1) {
            // Dragging
            mouse.accepted = true;
            if (zoneRoot.operationState !== 1)
                return ;

            // Validate canvas dimensions
            if (zoneRoot.canvasWidth <= 0 || zoneRoot.canvasHeight <= 0 || !isFinite(zoneRoot.canvasWidth) || !isFinite(zoneRoot.canvasHeight))
                return ;

            var canvasItem = zoneRoot.parent;
            var currentMouseInCanvas = dragHandler.mapToItem(canvasItem, mouse.x, mouse.y);
            // Validate mouse coordinates
            if (!isFinite(currentMouseInCanvas.x) || isNaN(currentMouseInCanvas.x) || !isFinite(currentMouseInCanvas.y) || isNaN(currentMouseInCanvas.y))
                return ;

            var dx = currentMouseInCanvas.x - mouseStart.x;
            var dy = currentMouseInCanvas.y - mouseStart.y;
            // Validate deltas
            if (!isFinite(dx) || isNaN(dx))
                dx = 0;

            if (!isFinite(dy) || isNaN(dy))
                dy = 0;

            // Only mark as dragged if movement exceeds threshold (5 pixels)
            var dragThreshold = 5;
            if (!hasDragged && (Math.abs(dx) > dragThreshold || Math.abs(dy) > dragThreshold))
                hasDragged = true;

            // Calculate new position with bounds checking
            var newX = Math.max(0, Math.min(zoneRoot.canvasWidth - zoneRoot.visualWidth, dragStart.x + dx));
            var newY = Math.max(0, Math.min(zoneRoot.canvasHeight - zoneRoot.visualHeight, dragStart.y + dy));
            // Ensure calculated positions are valid
            if (!isFinite(newX) || isNaN(newX))
                newX = dragStart.x;

            if (!isFinite(newY) || isNaN(newY))
                newY = dragStart.y;

            var finalX = newX;
            var finalY = newY;
            // Check if fill modifier is held - if so, skip snapping (fill takes precedence)
            var fillOnDropModifier = controller ? controller.fillOnDropModifier : Qt.ControlModifier;
            var fillOnDropEnabled = controller ? controller.fillOnDropEnabled : false;
            var fillModifierHeld = fillOnDropEnabled && ((mouse.modifiers & fillOnDropModifier) !== 0);
            // Apply snapping (only if fill modifier is NOT held)
            var baseSnappingEnabled = controller && (controller.gridSnappingEnabled || controller.edgeSnappingEnabled);
            var overrideModifier = controller ? controller.snapOverrideModifier : Qt.ShiftModifier;
            var modifierHeld = (mouse.modifiers & overrideModifier) !== 0;
            var snappingEnabled = !fillModifierHeld && (modifierHeld ? !baseSnappingEnabled : baseSnappingEnabled);
            if (snappingEnabled && zoneRoot.canvasWidth > 0 && zoneRoot.canvasHeight > 0 && isFinite(zoneRoot.canvasWidth) && isFinite(zoneRoot.canvasHeight) && isFinite(newX) && isFinite(newY) && isFinite(zoneRoot.visualWidth) && isFinite(zoneRoot.visualHeight)) {
                // Convert to relative coordinates for snapping
                var relX = newX / zoneRoot.canvasWidth;
                var relY = newY / zoneRoot.canvasHeight;
                var relW = zoneRoot.visualWidth / zoneRoot.canvasWidth;
                var relH = zoneRoot.visualHeight / zoneRoot.canvasHeight;
                // Validate relative coordinates
                if (!isFinite(relX) || isNaN(relX))
                    relX = 0;

                if (!isFinite(relY) || isNaN(relY))
                    relY = 0;

                if (!isFinite(relW) || isNaN(relW) || relW <= 0)
                    relW = zoneRoot.visualWidth / zoneRoot.canvasWidth;

                if (!isFinite(relH) || isNaN(relH) || relH <= 0)
                    relH = zoneRoot.visualHeight / zoneRoot.canvasHeight;

                var snapResult = controller.snapGeometry(relX, relY, relW, relH, zoneRoot.zoneId);
                if (snapResult && snapResult.x !== undefined && snapResult.y !== undefined && isFinite(snapResult.x) && !isNaN(snapResult.x) && isFinite(snapResult.y) && !isNaN(snapResult.y)) {
                    // Always use snapped position - edge snapping has priority over grid snapping
                    // in the SnappingService (edges that snap to zone edges won't grid-snap)
                    finalX = snapResult.x * zoneRoot.canvasWidth;
                    finalY = snapResult.y * zoneRoot.canvasHeight;
                    // Clamp to canvas bounds
                    finalX = Math.max(0, Math.min(zoneRoot.canvasWidth - zoneRoot.visualWidth, finalX));
                    finalY = Math.max(0, Math.min(zoneRoot.canvasHeight - zoneRoot.visualHeight, finalY));
                    // Ensure final coordinates are valid
                    if (!isFinite(finalX) || isNaN(finalX))
                        finalX = newX;

                    if (!isFinite(finalY) || isNaN(finalY))
                        finalY = newY;

                    // Show snap lines
                    if (snapIndicator) {
                        var originalX = dragStart.x / zoneRoot.canvasWidth;
                        var originalY = dragStart.y / zoneRoot.canvasHeight;
                        var originalW = zoneRoot.visualWidth / zoneRoot.canvasWidth;
                        var originalH = zoneRoot.visualHeight / zoneRoot.canvasHeight;
                        var originalRight = originalX + originalW;
                        var originalBottom = originalY + originalH;
                        var finalRelX = finalX / zoneRoot.canvasWidth;
                        var finalRelY = finalY / zoneRoot.canvasHeight;
                        var finalRight = finalRelX + originalW;
                        var finalBottom = finalRelY + originalH;
                        snapIndicator.setSnapLines(finalRelX, finalRelY, finalRight, finalBottom, originalX, originalY, originalRight, originalBottom, 0.005);
                    }
                } else {
                    if (snapIndicator)
                        snapIndicator.clearSnapLines();

                }
            } else {
                if (snapIndicator)
                    snapIndicator.clearSnapLines();

            }
            // Fill preview (reuse variables already computed above)
            var ctrlHeld = fillModifierHeld;
            if (ctrlHeld && controller && zoneRoot.zoneId) {
                var mouseNormX = currentMouseInCanvas.x / zoneRoot.canvasWidth;
                var mouseNormY = currentMouseInCanvas.y / zoneRoot.canvasHeight;
                var region = controller.calculateFillRegion(zoneRoot.zoneId, mouseNormX, mouseNormY);
                if (region && region.width !== undefined && region.width > 0) {
                    var regionChanged = !fillRegion || Math.abs(region.x - fillRegion.x) > 0.001 || Math.abs(region.y - fillRegion.y) > 0.001 || Math.abs(region.width - fillRegion.width) > 0.001 || Math.abs(region.height - fillRegion.height) > 0.001;
                    if (regionChanged) {
                        zoneRoot.animateFillPreview = true;
                        fillRegion = region;
                        committedFillRegion = region; // Store for commit on release
                        fillPreviewActive = true;
                        animateOffTimer.restart();
                    }
                    zoneRoot.visualX = fillRegion.x * zoneRoot.canvasWidth;
                    zoneRoot.visualY = fillRegion.y * zoneRoot.canvasHeight;
                    zoneRoot.visualWidth = fillRegion.width * zoneRoot.canvasWidth;
                    zoneRoot.visualHeight = fillRegion.height * zoneRoot.canvasHeight;
                } else {
                    if (fillPreviewActive) {
                        zoneRoot.animateFillPreview = true;
                        animateOffTimer.restart();
                    }
                    fillPreviewActive = false;
                    fillRegion = null;
                    committedFillRegion = null;
                    zoneRoot.visualX = finalX;
                    zoneRoot.visualY = finalY;
                    zoneRoot.visualWidth = originalSize.width;
                    zoneRoot.visualHeight = originalSize.height;
                }
            } else {
                // Modifier key released - keep visual at fill position if we had one
                // but stop updating fill preview
                if (fillPreviewActive) {
                    zoneRoot.animateFillPreview = true;
                    animateOffTimer.restart();
                }
                fillPreviewActive = false;
                // Don't clear fillRegion or committedFillRegion - preserve for commit on mouse release
                // Keep visual at current fill position instead of reverting
                if (committedFillRegion) {
                    zoneRoot.visualX = committedFillRegion.x * zoneRoot.canvasWidth;
                    zoneRoot.visualY = committedFillRegion.y * zoneRoot.canvasHeight;
                    zoneRoot.visualWidth = committedFillRegion.width * zoneRoot.canvasWidth;
                    zoneRoot.visualHeight = committedFillRegion.height * zoneRoot.canvasHeight;
                } else {
                    zoneRoot.visualX = finalX;
                    zoneRoot.visualY = finalY;
                    zoneRoot.visualWidth = originalSize.width;
                    zoneRoot.visualHeight = originalSize.height;
                }
            }
            zoneRoot.operationUpdated(zoneRoot.zoneId, zoneRoot.visualX, zoneRoot.visualY, zoneRoot.visualWidth, zoneRoot.visualHeight);
            // Update multi-zone drag if active (move other selected zones by the same delta)
            if (controller && controller.isMultiZoneDragActive() && zoneRoot.canvasWidth > 0 && zoneRoot.canvasHeight > 0) {
                var relX = zoneRoot.visualX / zoneRoot.canvasWidth;
                var relY = zoneRoot.visualY / zoneRoot.canvasHeight;
                controller.updateMultiZoneDrag(zoneRoot.zoneId, relX, relY);
            }
        }
    }
    onReleased: function(mouse) {
        if (zoneRoot.operationState === 1) {
            // Dragging
            if (snapIndicator)
                snapIndicator.clearSnapLines();

            animateOffTimer.stop();
            fillPreviewActive = false;
            zoneRoot.animateFillPreview = false;
            // Only update geometry if actual drag movement occurred
            if (hasDragged) {
                var relX, relY, relW, relH;
                // Use committed fill region if available (even if modifier key was released)
                if (committedFillRegion) {
                    relX = committedFillRegion.x;
                    relY = committedFillRegion.y;
                    relW = committedFillRegion.width;
                    relH = committedFillRegion.height;
                } else if (fillRegion) {
                    relX = fillRegion.x;
                    relY = fillRegion.y;
                    relW = fillRegion.width;
                    relH = fillRegion.height;
                } else {
                    relX = zoneRoot.toRelativeX(zoneRoot.visualX);
                    relY = zoneRoot.toRelativeY(zoneRoot.visualY);
                    relW = zoneRoot.toRelativeW(originalSize.width);
                    relH = zoneRoot.toRelativeH(originalSize.height);
                }
                zoneRoot.geometryChanged(relX, relY, relW, relH);
                // End multi-zone drag with commit
                if (controller && controller.isMultiZoneDragActive())
                    controller.endMultiZoneDrag(true);

            } else {
                // No actual drag movement - cancel multi-zone drag without commit
                if (controller && controller.isMultiZoneDragActive())
                    controller.endMultiZoneDrag(false);

            }
            fillRegion = null;
            committedFillRegion = null; // Clear after commit
            hasDragged = false;
            zoneRoot.operationState = 0; // Idle
            zoneRoot.operationEnded(zoneRoot.zoneId);
        }
    }
    onCanceled: {
        if (zoneRoot.operationState === 1) {
            // Dragging
            if (snapIndicator)
                snapIndicator.clearSnapLines();

            animateOffTimer.stop();
            fillPreviewActive = false;
            fillRegion = null;
            committedFillRegion = null;
            hasDragged = false;
            zoneRoot.animateFillPreview = false;
            zoneRoot.visualX = dragStart.x;
            zoneRoot.visualY = dragStart.y;
            zoneRoot.visualWidth = originalSize.width;
            zoneRoot.visualHeight = originalSize.height;
            // Cancel multi-zone drag without commit
            if (controller && controller.isMultiZoneDragActive())
                controller.endMultiZoneDrag(false);

            zoneRoot.operationState = 0; // Idle
            zoneRoot.operationEnded(zoneRoot.zoneId);
        }
    }

    // Timer to turn off animation after transition completes
    Timer {
        id: animateOffTimer

        interval: 200
        onTriggered: zoneRoot.animateFillPreview = false
    }

}
