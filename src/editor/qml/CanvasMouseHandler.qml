// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Mouse handler for canvas interactions
 *
 * Handles mouse events on the canvas for:
 * - Zone deselection (click empty space)
 * - Double-click zone creation
 * - Rectangle drag selection (click and drag to select multiple zones)
 * - Alt+drag selection (select through zones when Alt is held)
 *
 * Extracted from EditorWindow.qml to reduce file size.
 */
Item {
    id: canvasHandler

    // Required references
    required property var editorWindow
    required property var editorController
    required property Item drawingArea
    required property bool previewMode
    // Rectangle selection state (shared between both mouse areas)
    property bool isSelecting: false
    property point selectionStart: Qt.point(0, 0)
    property rect selectionRect: Qt.rect(0, 0, 0, 0)
    readonly property int dragThreshold: 5 // Minimum drag distance to start selection

    /**
     * @brief Updates the selection rectangle based on current mouse position
     * @param mouseX Current mouse X position
     * @param mouseY Current mouse Y position
     */
    function updateSelectionRect(mouseX, mouseY) {
        // Calculate selection rectangle (handle negative dimensions)
        var x = Math.min(selectionStart.x, mouseX);
        var y = Math.min(selectionStart.y, mouseY);
        var w = Math.abs(mouseX - selectionStart.x);
        var h = Math.abs(mouseY - selectionStart.y);
        // Clamp to canvas bounds
        x = Math.max(0, x);
        y = Math.max(0, y);
        if (x + w > drawingArea.width)
            w = drawingArea.width - x;

        if (y + h > drawingArea.height)
            h = drawingArea.height - y;

        selectionRect = Qt.rect(x, y, w, h);
    }

    /**
     * @brief Select zones that intersect with the selection rectangle
     * @param rect Selection rectangle in canvas pixel coordinates
     * @param additive If true, add to existing selection; if false, replace selection
     *
     * Uses C++ Q_INVOKABLE method for better performance during drag operations.
     */
    function selectZonesInRect(rect, additive) {
        if (!editorController || !drawingArea || rect.width < 1 || rect.height < 1)
            return;

        // Guard against division by zero
        if (drawingArea.width <= 0 || drawingArea.height <= 0)
            return;

        // Convert to relative coordinates and delegate to C++ for efficient iteration
        var relX = rect.x / drawingArea.width;
        var relY = rect.y / drawingArea.height;
        var relWidth = rect.width / drawingArea.width;
        var relHeight = rect.height / drawingArea.height;
        
        var selectedIds = editorController.selectZonesInRect(relX, relY, relWidth, relHeight, additive);
        
        // Update anchor for shift+click range selection
        if (selectedIds.length > 0 && editorWindow) {
            editorWindow.selectionAnchorId = selectedIds[selectedIds.length - 1];
        }
    }

    anchors.fill: parent

    // Selection rectangle visual - parented to drawingArea for correct coordinates
    Rectangle {
        id: selectionVisual

        parent: canvasHandler.drawingArea
        visible: canvasHandler.isSelecting && (width > 2 || height > 2)
        x: canvasHandler.selectionRect.x
        y: canvasHandler.selectionRect.y
        width: canvasHandler.selectionRect.width
        height: canvasHandler.selectionRect.height
        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2)
        border.color: Kirigami.Theme.highlightColor
        border.width: 1
        z: 1000 // Above everything
    }

    /**
     * @brief Alt+drag overlay - sits above zones to capture Alt+drag for selection
     *
     * This overlay captures mouse events when Alt is held, allowing rectangle
     * selection even when starting on top of zones.
     * Parented to drawingArea directly so z-index works relative to zones.
     */
    MouseArea {
        // Don't propagate composed events - we handle selection ourselves

        id: altDragOverlay

        property bool altDragActive: false

        // Parent to drawingArea so z-index is relative to zones (siblings)
        parent: canvasHandler.drawingArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: 100 // Above zones (zones are typically z: 0-10)
        hoverEnabled: false
        onPressed: function(mouse) {
            // Only capture if Alt is held - otherwise let zones handle it
            if (mouse.modifiers & Qt.AltModifier) {
                mouse.accepted = true;
                altDragActive = true;
                canvasHandler.selectionStart = Qt.point(mouse.x, mouse.y);
                canvasHandler.isSelecting = false;
                canvasHandler.selectionRect = Qt.rect(mouse.x, mouse.y, 0, 0);
                // Clear selection unless Ctrl is also held (additive mode)
                if (!(mouse.modifiers & Qt.ControlModifier)) {
                    if (canvasHandler.editorController)
                        canvasHandler.editorController.clearSelection();

                }
            } else {
                mouse.accepted = false; // Let zones or background handler handle it
                altDragActive = false;
            }
        }
        onPositionChanged: function(mouse) {
            if (!altDragActive || !pressed) {
                mouse.accepted = false;
                return ;
            }
            var dx = mouse.x - canvasHandler.selectionStart.x;
            var dy = mouse.y - canvasHandler.selectionStart.y;
            var distance = Math.sqrt(dx * dx + dy * dy);
            // Start selection if dragged past threshold
            if (!canvasHandler.isSelecting && distance > canvasHandler.dragThreshold)
                canvasHandler.isSelecting = true;

            if (canvasHandler.isSelecting)
                canvasHandler.updateSelectionRect(mouse.x, mouse.y);

            mouse.accepted = true;
        }
        onReleased: function(mouse) {
            if (altDragActive) {
                if (canvasHandler.isSelecting)
                    canvasHandler.selectZonesInRect(canvasHandler.selectionRect, mouse.modifiers & Qt.ControlModifier);

                canvasHandler.isSelecting = false;
                canvasHandler.selectionRect = Qt.rect(0, 0, 0, 0);
                altDragActive = false;
                mouse.accepted = true;
            } else {
                mouse.accepted = false;
            }
        }
        onCanceled: {
            altDragActive = false;
            canvasHandler.isSelecting = false;
            canvasHandler.selectionRect = Qt.rect(0, 0, 0, 0);
        }
    }

    /**
     * @brief Background mouse handler - handles clicks on empty canvas space
     */
    MouseArea {
        id: backgroundHandler

        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: -1 // Below zones so zone clicks work
        propagateComposedEvents: false
        // Ensure drawingArea maintains focus when clicking empty space
        onClicked: function(mouse) {
            canvasHandler.drawingArea.forceActiveFocus();
        }
        onPressed: function(mouse) {
            // Start potential rectangle selection
            canvasHandler.selectionStart = Qt.point(mouse.x, mouse.y);
            canvasHandler.isSelecting = false; // Will become true if dragged past threshold
            canvasHandler.selectionRect = Qt.rect(mouse.x, mouse.y, 0, 0);
            mouse.accepted = true;
        }
        onPositionChanged: function(mouse) {
            if (!pressed) {
                mouse.accepted = false;
                return ;
            }
            var dx = mouse.x - canvasHandler.selectionStart.x;
            var dy = mouse.y - canvasHandler.selectionStart.y;
            var distance = Math.sqrt(dx * dx + dy * dy);
            // Start selection if dragged past threshold
            if (!canvasHandler.isSelecting && distance > canvasHandler.dragThreshold) {
                canvasHandler.isSelecting = true;
                // Clear existing selection when starting new rectangle selection
                // unless Ctrl is held (additive selection)
                if (!(mouse.modifiers & Qt.ControlModifier)) {
                    if (canvasHandler.editorController)
                        canvasHandler.editorController.clearSelection();

                }
            }
            if (canvasHandler.isSelecting)
                canvasHandler.updateSelectionRect(mouse.x, mouse.y);

            mouse.accepted = true;
        }
        onReleased: function(mouse) {
            if (canvasHandler.isSelecting) {
                // Select all zones within the rectangle
                canvasHandler.selectZonesInRect(canvasHandler.selectionRect, mouse.modifiers & Qt.ControlModifier);
                canvasHandler.isSelecting = false;
                canvasHandler.selectionRect = Qt.rect(0, 0, 0, 0);
            } else {
                // Simple click on empty space - clear selection
                if (canvasHandler.editorController)
                    canvasHandler.editorController.clearSelection();

            }
            mouse.accepted = true;
        }
        onCanceled: {
            canvasHandler.isSelecting = false;
            canvasHandler.selectionRect = Qt.rect(0, 0, 0, 0);
        }
        onDoubleClicked: function(mouse) {
            if (canvasHandler.previewMode)
                return ;
            if (!canvasHandler.editorController || !canvasHandler.drawingArea)
                return ;

            // Quick add zone at click position
            var relX = Math.max(0, (mouse.x / canvasHandler.drawingArea.width) - 0.125);
            var relY = Math.max(0, (mouse.y / canvasHandler.drawingArea.height) - 0.125);
            var relW = 0.25;
            var relH = 0.25;
            // Ensure zone fits in canvas
            if (relX + relW > 1)
                relX = 1 - relW;

            if (relY + relH > 1)
                relY = 1 - relH;

            canvasHandler.editorController.addZone(relX, relY, relW, relH);
        }
    }

}
