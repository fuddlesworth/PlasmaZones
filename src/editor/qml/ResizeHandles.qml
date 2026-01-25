// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Resize handles component for zone resizing
 * 
 * Provides 8 resize handles (4 corners + 4 edges) for zone resizing.
 * Handles resize operations with snapping support.
 */
Item {
    id: resizeHandles
    
    // Constants for visual styling
    QtObject {
        id: constants
        readonly property real handleBorderWidth: 1.5  // 1.5px - handle border width (visual style)
        readonly property int shadowBorderWidth: 1  // 1px - shadow border width
    }
    
    anchors.fill: parent
    
    required property var root  // Parent zone component
    required property real canvasWidth  // Canvas width (may be 0 during initialization)
    required property real canvasHeight  // Canvas height (may be 0 during initialization)
    required property real handleSize
    required property real minSize
    required property var zoneData
    property var snapIndicator: null
    
    // Get actual canvas dimensions dynamically with fallback to root
    readonly property real actualCanvasWidth: {
        // Try passed property first
        if (resizeHandles.canvasWidth > 0 && isFinite(resizeHandles.canvasWidth)) {
            return resizeHandles.canvasWidth
        }
        // Fallback to root's canvasWidth if available
        if (resizeHandles.root && resizeHandles.root.canvasWidth !== undefined && 
            resizeHandles.root.canvasWidth > 0 && isFinite(resizeHandles.root.canvasWidth)) {
            return resizeHandles.root.canvasWidth
        }
        return 0
    }
    readonly property real actualCanvasHeight: {
        // Try passed property first
        if (resizeHandles.canvasHeight > 0 && isFinite(resizeHandles.canvasHeight)) {
            return resizeHandles.canvasHeight
        }
        // Fallback to root's canvasHeight if available
        if (resizeHandles.root && resizeHandles.root.canvasHeight !== undefined && 
            resizeHandles.root.canvasHeight > 0 && isFinite(resizeHandles.root.canvasHeight)) {
            return resizeHandles.root.canvasHeight
        }
        return 0
    }
    
    // Track if canvas has valid dimensions for handle visibility
    readonly property bool canvasReady: actualCanvasWidth > 0 && actualCanvasHeight > 0

    // Handle positions: nw, n, ne, e, se, s, sw, w
    Repeater {
        model: [
            { id: "nw", ax: 0, ay: 0, cursor: Qt.SizeFDiagCursor },
            { id: "n",  ax: 0.5, ay: 0, cursor: Qt.SizeVerCursor },
            { id: "ne", ax: 1, ay: 0, cursor: Qt.SizeBDiagCursor },
            { id: "e",  ax: 1, ay: 0.5, cursor: Qt.SizeHorCursor },
            { id: "se", ax: 1, ay: 1, cursor: Qt.SizeFDiagCursor },
            { id: "s",  ax: 0.5, ay: 1, cursor: Qt.SizeVerCursor },
            { id: "sw", ax: 0, ay: 1, cursor: Qt.SizeBDiagCursor },
            { id: "w",  ax: 0, ay: 0.5, cursor: Qt.SizeHorCursor }
        ]

        Rectangle {
            id: handle
            required property var modelData
            required property int index


            // Use root's visual dimensions instead of parent dimensions
            // Root is EditorZone, which has visualWidth/visualHeight (actual zone size)
            // Parent is ResizeHandles Item with anchors.fill: parent (EditorZone)
            // But parent width/height has spacing subtracted, so use root.visualWidth/Height instead
            readonly property bool hasValidRoot: resizeHandles.root !== null && 
                                                 resizeHandles.root !== undefined
            // Get zone dimensions - use visualWidth/Height if available, otherwise calculate from zoneData
            readonly property real zoneW: {
                if (!hasValidRoot) return 0
                if (isFinite(resizeHandles.root.visualWidth) && resizeHandles.root.visualWidth > 0) {
                    return resizeHandles.root.visualWidth
                }
                // Fallback: calculate from zoneData if visualWidth not set yet
                if (resizeHandles.zoneData && resizeHandles.canvasWidth > 0) {
                    var w = resizeHandles.zoneData.width || resizeHandles.zoneData.relativeGeometry?.width || 0.25
                    return w * resizeHandles.canvasWidth
                }
                return 0
            }
            readonly property real zoneH: {
                if (!hasValidRoot) return 0
                if (isFinite(resizeHandles.root.visualHeight) && resizeHandles.root.visualHeight > 0) {
                    return resizeHandles.root.visualHeight
                }
                // Fallback: calculate from zoneData if visualHeight not set yet
                if (resizeHandles.zoneData && resizeHandles.canvasHeight > 0) {
                    var h = resizeHandles.zoneData.height || resizeHandles.zoneData.relativeGeometry?.height || 0.25
                    return h * resizeHandles.canvasHeight
                }
                return 0
            }
            // Check if zone has reasonable size for showing handles
            readonly property bool hasValidZoneSize: zoneW > 20 && zoneH > 20
            // But parent (EditorZone) has spacing-adjusted dimensions, so use that for bounds
            readonly property bool hasValidParent: resizeHandles.parent !== null && 
                                                  resizeHandles.parent !== undefined
            readonly property real parentW: hasValidParent ? resizeHandles.parent.width : 0
            readonly property real parentH: hasValidParent ? resizeHandles.parent.height : 0

            // Handle type detection
            readonly property bool isCornerHandle: modelData.id.length === 2  // nw, ne, se, sw
            readonly property bool isHorizontalEdge: modelData.id === "n" || modelData.id === "s"
            readonly property bool isVerticalEdge: modelData.id === "e" || modelData.id === "w"

            // Ensure handle size has a fallback
            readonly property real effectiveHandleSize: resizeHandles.handleSize > 0 ? resizeHandles.handleSize : 12

            // Corner handles: small circles (10px diameter)
            // Edge handles: thin pills (4px thick, 24px long)
            readonly property real cornerSize: 10
            readonly property real edgeThickness: 4
            readonly property real edgeLength: 24

            width: isCornerHandle ? cornerSize : (isHorizontalEdge ? edgeLength : edgeThickness)
            height: isCornerHandle ? cornerSize : (isVerticalEdge ? edgeLength : edgeThickness)

            // Position handles at corners/edges
            x: modelData.ax * parent.width - width / 2
            y: modelData.ay * parent.height - height / 2

            // Corner handles: fully circular, Edge handles: pill-shaped
            radius: isCornerHandle ? cornerSize / 2 : edgeThickness / 2

            // Clean white fill with subtle border
            color: handleMouse.containsMouse || handleMouse.pressed ?
                   Kirigami.Theme.highlightColor :
                   Kirigami.Theme.backgroundColor
            border.color: handleMouse.containsMouse || handleMouse.pressed ?
                          Kirigami.Theme.highlightColor :
                          Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.6)
            border.width: constants.handleBorderWidth

            // Handles are always present for mouse detection, only visible when hovered/selected
            visible: parent.width > 0 && parent.height > 0
            opacity: (resizeHandles.root.isSelected || resizeHandles.root.mouseOverZone) ? 1.0 : 0.0
            z: 200

            // Smooth fade in/out
            Behavior on opacity { NumberAnimation { duration: 150 } }

            // Smooth hover transitions
            Behavior on color { ColorAnimation { duration: 100 } }
            Behavior on border.color { ColorAnimation { duration: 100 } }

            // Drop shadow for depth
            Rectangle {
                id: handleShadow
                anchors.centerIn: parent
                width: parent.width + 2
                height: parent.height + 2
                z: -1
                radius: parent.radius + 1
                color: "transparent"
                // Use theme text color for contrast (works in both light and dark themes)
                border.color: Qt.rgba(
                    Kirigami.Theme.textColor.r,
                    Kirigami.Theme.textColor.g,
                    Kirigami.Theme.textColor.b,
                    0.3
                )
                border.width: constants.shadowBorderWidth
                visible: parent.visible
            }

            MouseArea {
                id: handleMouse
                anchors.fill: parent
                anchors.margins: -4  // Larger hit area for easier grabbing
                hoverEnabled: true
                cursorShape: modelData.cursor
                preventStealing: true
                propagateComposedEvents: false
                z: 100
                acceptedButtons: Qt.LeftButton

                // Accessibility properties with dynamic handle names
                Accessible.role: Accessible.Button
                Accessible.name: {
                    var handleNames = {
                        "nw": i18nc("@action:button", "Northwest corner resize handle"),
                        "n": i18nc("@action:button", "North edge resize handle"),
                        "ne": i18nc("@action:button", "Northeast corner resize handle"),
                        "e": i18nc("@action:button", "East edge resize handle"),
                        "se": i18nc("@action:button", "Southeast corner resize handle"),
                        "s": i18nc("@action:button", "South edge resize handle"),
                        "sw": i18nc("@action:button", "Southwest corner resize handle"),
                        "w": i18nc("@action:button", "West edge resize handle")
                    }
                    return handleNames[modelData.id] || i18nc("@action:button", "Resize handle")
                }
                Accessible.description: {
                    var handleDescriptions = {
                        "nw": i18nc("@info:tooltip", "Drag to resize zone from northwest corner"),
                        "n": i18nc("@info:tooltip", "Drag to resize zone from top edge"),
                        "ne": i18nc("@info:tooltip", "Drag to resize zone from northeast corner"),
                        "e": i18nc("@info:tooltip", "Drag to resize zone from right edge"),
                        "se": i18nc("@info:tooltip", "Drag to resize zone from southeast corner"),
                        "s": i18nc("@info:tooltip", "Drag to resize zone from bottom edge"),
                        "sw": i18nc("@info:tooltip", "Drag to resize zone from southwest corner"),
                        "w": i18nc("@info:tooltip", "Drag to resize zone from left edge")
                    }
                    return handleDescriptions[modelData.id] || i18nc("@info:tooltip", "Drag to resize zone")
                }

                onEntered: {
                    resizeHandles.root.anyHandleHovered = true
                }

                onExited: {
                    resizeHandles.root.anyHandleHovered = false
                }
                
                onPressedChanged: {
                    // Clear hover state when starting resize to avoid conflicts
                    if (pressed) {
                        resizeHandles.root.anyHandleHovered = true
                    }
                }
                
                // Store initial state for resize calculations
                property real startMouseCanvasX: 0  // Initial mouse X in canvas coordinates
                property real startMouseCanvasY: 0  // Initial mouse Y in canvas coordinates
                property real startZoneX: 0         // Initial zone X in canvas coordinates
                property real startZoneY: 0         // Initial zone Y in canvas coordinates
                property real startZoneW: 0         // Initial zone width in canvas coordinates
                property real startZoneH: 0         // Initial zone height in canvas coordinates

                onPressed: function(mouse) {
                    var actualCanvasW = resizeHandles.actualCanvasWidth
                    var actualCanvasH = resizeHandles.actualCanvasHeight
                    
                    // Validate dimensions before proceeding
                    if (!resizeHandles.root ||
                        !isFinite(actualCanvasW) ||
                        !isFinite(actualCanvasH) ||
                        actualCanvasW <= 0 ||
                        actualCanvasH <= 0) {
                        mouse.accepted = false
                        return
                    }

                    // Check if handle is visible and positioned correctly
                    if (!handle.visible || handle.x < -1000 || handle.y < -1000) {
                        mouse.accepted = false
                        return
                    }
                    
                    mouse.accepted = true
                    // State values: 0=Idle, 1=Dragging, 2=Resizing
                    if (resizeHandles.root.operationState === 0) {
                        resizeHandles.root.operationState = 2;  // Resizing
                        // Store initial zone state in canvas coordinates
                        // Use visualX/Y/Width/Height (actual zone geometry) NOT root.x/y/width/height
                        // (which have spacing offsets applied)
                        // Guard against NaN values
                        startZoneX = isFinite(resizeHandles.root.visualX) ? resizeHandles.root.visualX : 0
                        startZoneY = isFinite(resizeHandles.root.visualY) ? resizeHandles.root.visualY : 0
                        startZoneW = isFinite(resizeHandles.root.visualWidth) ? resizeHandles.root.visualWidth : resizeHandles.minSize
                        startZoneH = isFinite(resizeHandles.root.visualHeight) ? resizeHandles.root.visualHeight : resizeHandles.minSize

                        // Clamp initial values to valid range
                        startZoneX = Math.max(0, Math.min(actualCanvasW, startZoneX))
                        startZoneY = Math.max(0, Math.min(actualCanvasH, startZoneY))
                        startZoneW = Math.max(resizeHandles.minSize, Math.min(actualCanvasW - startZoneX, startZoneW))
                        startZoneH = Math.max(resizeHandles.minSize, Math.min(actualCanvasH - startZoneY, startZoneH))

                        handleMouse.actualCanvasWidth = actualCanvasW
                        handleMouse.actualCanvasHeight = actualCanvasH
                        
                        // Map initial mouse position to canvas coordinates
                        var canvasItem = resizeHandles.root.parent
                        if (!canvasItem) {
                            resizeHandles.root.operationState = 0
                            return
                        }
                        var mouseInCanvas = handleMouse.mapToItem(canvasItem, mouse.x, mouse.y)
                        startMouseCanvasX = isFinite(mouseInCanvas.x) ? mouseInCanvas.x : 0
                        startMouseCanvasY = isFinite(mouseInCanvas.y) ? mouseInCanvas.y : 0
                        // Signal operation started
                        resizeHandles.root.operationStarted(resizeHandles.root.zoneId, startZoneX, startZoneY, startZoneW, startZoneH);
                    }
                }
                
                // Store actual canvas dimensions for use during resize
                property real actualCanvasWidth: resizeHandles.canvasWidth
                property real actualCanvasHeight: resizeHandles.canvasHeight

                onPositionChanged: function(mouse) {
                    // State 2 = Resizing
                    if (!pressed || resizeHandles.root.operationState !== 2) {
                        return
                    }

                    var actualW = resizeHandles.actualCanvasWidth
                    var actualH = resizeHandles.actualCanvasHeight
                    
                    // Fallback: Use stored values from onPressed if actualCanvas properties still invalid
                    if ((actualW <= 0 || actualH <= 0) && handleMouse.actualCanvasWidth > 0) {
                        actualW = handleMouse.actualCanvasWidth
                        actualH = handleMouse.actualCanvasHeight
                    }

                    // Validate dimensions before proceeding
                    if (!isFinite(actualW) || !isFinite(actualH) || actualW <= 0 || actualH <= 0) {
                        return
                    }

                    mouse.accepted = true

                    // Map current mouse position to canvas coordinates
                    var canvasItem = resizeHandles.root.parent
                    if (!canvasItem) return
                    var currentMouseInCanvas = handleMouse.mapToItem(canvasItem, mouse.x, mouse.y)
                    
                    // Calculate delta from initial mouse position (both in canvas coordinates)
                    // Guard against NaN
                    var dx = (isFinite(currentMouseInCanvas.x) && isFinite(startMouseCanvasX)) ? 
                             (currentMouseInCanvas.x - startMouseCanvasX) : 0
                    var dy = (isFinite(currentMouseInCanvas.y) && isFinite(startMouseCanvasY)) ? 
                             (currentMouseInCanvas.y - startMouseCanvasY) : 0

                    var newX = startZoneX;
                    var newY = startZoneY;
                    var newW = startZoneW;
                    var newH = startZoneH;

                    // Resize based on handle position - explicit handle type checking
                    var hid = modelData.id;
                    var isWest = hid.includes("w");
                    var isEast = hid.includes("e");
                    var isNorth = hid.includes("n");
                    var isSouth = hid.includes("s");

                    // Apply resize based on handle type - apply delta directly first
                    // Corner handles adjust both dimensions simultaneously
                    if (isWest) {
                        // West edge (nw, w, sw) - move left edge, adjust width
                        newX = startZoneX + dx;
                        newW = startZoneW - dx;
                    } else if (isEast) {
                        // East edge (ne, e, se) - adjust width only
                        newW = startZoneW + dx;
                        // newX stays at startZoneX
                    }

                    if (isNorth) {
                        // North edge (nw, n, ne) - move top edge, adjust height
                        newY = startZoneY + dy;
                        newH = startZoneH - dy;
                    } else if (isSouth) {
                        // South edge (sw, s, se) - adjust height only
                        newH = startZoneH + dy;
                        // newY stays at startZoneY
                    }

                    // Apply clamping AFTER calculating both dimensions
                    // Clamp horizontal (X and width)
                    if (isWest) {
                        // Constrain X to valid range, then recalculate width to maintain relationship
                        var minX = startZoneX + startZoneW - resizeHandles.minSize;
                        newX = Math.max(0, Math.min(minX, newX));
                        // Recalculate width from constrained X to prevent jumps
                        newW = startZoneW - (newX - startZoneX);
                        // Ensure minimum width is maintained
                        if (newW < resizeHandles.minSize) {
                            newW = resizeHandles.minSize;
                            newX = Math.max(0, startZoneX + startZoneW - resizeHandles.minSize);
                        }
                    } else if (isEast) {
                        // Constrain width directly
                        newW = Math.max(resizeHandles.minSize, Math.min(actualW - startZoneX, newW));
                    }
                    
                    // Clamp vertical (Y and height)
                    if (isNorth) {
                        // Constrain Y to valid range, then recalculate height to maintain relationship
                        var minY = startZoneY + startZoneH - resizeHandles.minSize;
                        newY = Math.max(0, Math.min(minY, newY));
                        // Recalculate height from constrained Y to prevent jumps
                        newH = startZoneH - (newY - startZoneY);
                        // Ensure minimum height is maintained
                        if (newH < resizeHandles.minSize) {
                            newH = resizeHandles.minSize;
                            newY = Math.max(0, startZoneY + startZoneH - resizeHandles.minSize);
                        }
                    } else if (isSouth) {
                        // Constrain height directly
                        newH = Math.max(resizeHandles.minSize, Math.min(actualH - startZoneY, newH));
                    }
                    
                    // Final bounds check - ensure zone stays within canvas
                    // Adjust width if zone extends beyond canvas
                    if (newX + newW > actualW) {
                        newW = actualW - newX;
                        // Ensure minimum width
                        if (newW < resizeHandles.minSize) {
                            newW = resizeHandles.minSize;
                            newX = actualW - resizeHandles.minSize;
                        }
                    }
                    // Adjust height if zone extends beyond canvas
                    if (newY + newH > actualH) {
                        newH = actualH - newY;
                        // Ensure minimum height
                        if (newH < resizeHandles.minSize) {
                            newH = resizeHandles.minSize;
                            newY = actualH - resizeHandles.minSize;
                        }
                    }
                    // Final minimum size validation
                    if (newW < resizeHandles.minSize) {
                        newW = resizeHandles.minSize;
                        if (isWest && newX + newW > actualW) {
                            newX = actualW - resizeHandles.minSize;
                        }
                    }
                    if (newH < resizeHandles.minSize) {
                        newH = resizeHandles.minSize;
                        if (isNorth && newY + newH > actualH) {
                            newY = actualH - resizeHandles.minSize;
                        }
                    }

                    // Apply snapping after clamping
                    // Only snap the edges being moved by this handle
                    // Modifier key overrides snapping behavior
                    var hasValidDimensions = resizeHandles.root.controller && actualW > 0 && actualH > 0
                    var baseSnappingEnabled = hasValidDimensions &&
                                             (resizeHandles.root.controller.gridSnappingEnabled || resizeHandles.root.controller.edgeSnappingEnabled)
                    var overrideModifier = resizeHandles.root.controller ? resizeHandles.root.controller.snapOverrideModifier : Qt.ShiftModifier
                    var modifierHeld = (mouse.modifiers & overrideModifier) !== 0
                    var shouldSnap = hasValidDimensions && (modifierHeld ? !baseSnappingEnabled : baseSnappingEnabled)

                    if (shouldSnap) {
                        // Convert to relative coordinates for snapping
                        // Guard against division by zero or NaN
                        var relX = (newX >= 0 && isFinite(newX)) ? newX / actualW : 0;
                        var relY = (newY >= 0 && isFinite(newY)) ? newY / actualH : 0;
                        var relW = (newW > 0 && isFinite(newW)) ? newW / actualW : resizeHandles.minSize / actualW;
                        var relH = (newH > 0 && isFinite(newH)) ? newH / actualH : resizeHandles.minSize / actualH;
                        
                        // Ensure relative values are valid
                        if (!isFinite(relX) || isNaN(relX)) relX = 0
                        if (!isFinite(relY) || isNaN(relY)) relY = 0
                        if (!isFinite(relW) || isNaN(relW) || relW <= 0) relW = (actualW > 0) ? resizeHandles.minSize / actualW : 0.05
                        if (!isFinite(relH) || isNaN(relH) || relH <= 0) relH = (actualH > 0) ? resizeHandles.minSize / actualH : 0.05
                        
                        // Determine which edges to snap based on handle being dragged
                        // Only snap edges that are actually being moved by this handle
                        var snapLeft = isWest;   // West handles move left edge
                        var snapRight = isEast;  // East handles move right edge
                        var snapTop = isNorth;   // North handles move top edge
                        var snapBottom = isSouth; // South handles move bottom edge
                        
                        // Apply selective snapping (only snap edges being moved)
                        // This prevents jumping because we only snap the edge being dragged
                        var snapped = resizeHandles.root.controller.snapGeometrySelective(
                            relX, relY, relW, relH, resizeHandles.root.zoneId,
                            snapLeft, snapRight, snapTop, snapBottom
                        );
                        
                        // Convert back to canvas coordinates
                        if (snapped && 
                            isFinite(snapped.x) && !isNaN(snapped.x) &&
                            isFinite(snapped.y) && !isNaN(snapped.y) &&
                            isFinite(snapped.width) && !isNaN(snapped.width) && snapped.width > 0 &&
                            isFinite(snapped.height) && !isNaN(snapped.height) && snapped.height > 0) {
                            
                            var snappedX = snapped.x * actualW;
                            var snappedY = snapped.y * actualH;
                            var snappedW = snapped.width * actualW;
                            var snappedH = snapped.height * actualH;
                            
                            // Apply snapping only to edges that were set to snap (prevents jumping)
                            // This is the key fix: only snap the edges being moved, preserve others
                            if (snapLeft) {
                                // Left edge moved - adjust both X and width to maintain right edge
                                var rightEdge = newX + newW;
                                newX = snappedX;
                                newW = rightEdge - newX;
                                // Ensure minimum width
                                if (newW < resizeHandles.minSize) {
                                    newW = resizeHandles.minSize;
                                    newX = rightEdge - newW;
                                }
                            } else if (snapRight) {
                                // Right edge moved - adjust width only (X stays same)
                                var rightEdge = snappedX + snappedW;
                                newW = rightEdge - newX;
                                if (newW < resizeHandles.minSize) {
                                    newW = resizeHandles.minSize;
                                    newX = rightEdge - newW;
                                }
                            }
                            
                            if (snapTop) {
                                // Top edge moved - adjust both Y and height to maintain bottom edge
                                var bottomEdge = newY + newH;
                                newY = snappedY;
                                newH = bottomEdge - newY;
                                // Ensure minimum height
                                if (newH < resizeHandles.minSize) {
                                    newH = resizeHandles.minSize;
                                    newY = bottomEdge - newH;
                                }
                            } else if (snapBottom) {
                                // Bottom edge moved - adjust height only (Y stays same)
                                var bottomEdge = snappedY + snappedH;
                                newH = bottomEdge - newY;
                                if (newH < resizeHandles.minSize) {
                                    newH = resizeHandles.minSize;
                                    newY = bottomEdge - newH;
                                }
                            }
                            
                            // Show snap lines for visual feedback
                            if (resizeHandles.snapIndicator && actualW > 0 && actualH > 0) {
                                // Calculate original position (before snapping) in relative coords
                                var origX = startZoneX / actualW;
                                var origY = startZoneY / actualH;
                                var origW = startZoneW / actualW;
                                var origH = startZoneH / actualH;
                                var origRight = origX + origW;
                                var origBottom = origY + origH;
                                
                                // Calculate snapped position in relative coords
                                var finalRelX = newX / actualW;
                                var finalRelY = newY / actualH;
                                var finalRelW = newW / actualW;
                                var finalRelH = newH / actualH;
                                var finalRight = finalRelX + finalRelW;
                                var finalBottom = finalRelY + finalRelH;
                                
                                // Show snap lines
                                resizeHandles.snapIndicator.setSnapLines(
                                    finalRelX, finalRelY, finalRight, finalBottom,
                                    origX, origY, origRight, origBottom,
                                    0.005  // threshold
                                );
                            }
                            
                            // Final bounds check after snapping
                            var beforeBoundsY = newY;
                            var beforeBoundsH = newH;
                            if (newX < 0) {
                                newW = newW + newX;  // Reduce width by overflow
                                newX = 0;
                            }
                            if (newY < 0) {
                                newH = newH + newY;  // Reduce height by overflow
                                newY = 0;
                            }
                            if (newX + newW > actualW) {
                                newW = actualW - newX;
                            }
                            if (newY + newH > actualH) {
                                newH = actualH - newY;
                            }
                            
                            // Ensure minimum size after all adjustments
                            if (newW < resizeHandles.minSize) {
                                newW = resizeHandles.minSize;
                                if (newX + newW > actualW) {
                                    newX = actualW - resizeHandles.minSize;
                                }
                            }
                            if (newH < resizeHandles.minSize) {
                                newH = resizeHandles.minSize;
                                if (newY + newH > actualH) {
                                    newY = actualH - resizeHandles.minSize;
                                }
                            }
                        }
                    }
                    
                    // Final validation - ensure all values are finite before assignment
                    if (!isFinite(newX) || isNaN(newX)) newX = startZoneX
                    if (!isFinite(newY) || isNaN(newY)) newY = startZoneY
                    if (!isFinite(newW) || isNaN(newW) || newW <= 0) newW = startZoneW
                    if (!isFinite(newH) || isNaN(newH) || newH <= 0) newH = startZoneH

                    // Update visual position directly (binding is disabled during resize)
                    resizeHandles.root.visualX = newX;
                    resizeHandles.root.visualY = newY;
                    resizeHandles.root.visualWidth = newW;
                    resizeHandles.root.visualHeight = newH;
                    // Signal operation updated
                    resizeHandles.root.operationUpdated(resizeHandles.root.zoneId, newX, newY, newW, newH);
                }

                onReleased: {
                    // State 2 = Resizing, 0 = Idle
                    // Store references early to avoid scope issues
                    var rootItem = resizeHandles.root
                    if (!rootItem || rootItem.operationState !== 2) {
                        return
                    }
                    
                    // Clear snap lines before committing
                    if (resizeHandles.snapIndicator) {
                        resizeHandles.snapIndicator.clearSnapLines();
                    }
                    
                    // Set state to Idle first so onZoneGeometryChanged can process
                    var actualW = handleMouse.actualCanvasWidth > 0 ? handleMouse.actualCanvasWidth : 
                                 (rootItem && rootItem.canvasWidth > 0 ? rootItem.canvasWidth : resizeHandles.canvasWidth)
                    var actualH = handleMouse.actualCanvasHeight > 0 ? handleMouse.actualCanvasHeight : 
                                 (rootItem && rootItem.canvasHeight > 0 ? rootItem.canvasHeight : resizeHandles.canvasHeight)
                    
                    if (actualW <= 0 || actualH <= 0) {
                        rootItem.operationState = 0
                        rootItem.operationEnded(rootItem.zoneId)
                        return
                    }
                    
                    // Convert to relative coordinates for C++ update
                    var relX = (actualW > 0 && isFinite(rootItem.visualX)) ? rootItem.visualX / actualW : 0
                    var relY = (actualH > 0 && isFinite(rootItem.visualY)) ? rootItem.visualY / actualH : 0
                    var relW = (actualW > 0 && isFinite(rootItem.visualWidth) && rootItem.visualWidth > 0) ? rootItem.visualWidth / actualW : 0.25
                    var relH = (actualH > 0 && isFinite(rootItem.visualHeight) && rootItem.visualHeight > 0) ? rootItem.visualHeight / actualH : 0.25

                    // Set state to Idle BEFORE committing so onZoneGeometryChanged can process
                    rootItem.operationState = 0;  // Idle

                    // Commit to model - this triggers C++ updateZoneGeometry
                    rootItem.geometryChanged(relX, relY, relW, relH)
                    
                    // Signal operation ended (state is already Idle)
                    var rootRef = rootItem
                    var zoneIdRef = rootItem.zoneId
                    Qt.callLater(function() {
                        if (rootRef) {
                            rootRef.operationEnded(zoneIdRef);
                        }
                    })
                }

                onCanceled: {
                    // State 2 = Resizing, 0 = Idle
                    if (resizeHandles.root.operationState === 2) {
                        // Clear snap lines on cancel
                        if (resizeHandles.snapIndicator) {
                            resizeHandles.snapIndicator.clearSnapLines();
                        }
                        // Reset to position at start of resize
                        resizeHandles.root.visualX = handleMouse.startZoneX;
                        resizeHandles.root.visualY = handleMouse.startZoneY;
                        resizeHandles.root.visualWidth = handleMouse.startZoneW;
                        resizeHandles.root.visualHeight = handleMouse.startZoneH;
                        resizeHandles.root.operationState = 0;  // Idle
                        // Signal operation ended
                        resizeHandles.root.operationEnded(resizeHandles.root.zoneId);
                    }
                }
            }
        }
    }
}
