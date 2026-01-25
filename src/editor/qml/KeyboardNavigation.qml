// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick

/**
 * @brief Keyboard navigation handler for zone selection and manipulation
 *
 * Handles Ctrl+Tab zone navigation and arrow key movement/resize.
 * Extracted from EditorWindow.qml to reduce file size.
 */
Item {
    id: keyboardNav

    // Required properties
    required property var editorController
    required property Item drawingArea

    // Handle keyboard events - call this from parent's Keys.onPressed
    function handleKeyPress(event) {
        if (!editorController) {
            event.accepted = false;
            return false;
        }
        // Skip modifier-only keys
        var isModifierOnly = (event.key === Qt.Key_Shift && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) || (event.key === Qt.Key_Control) || (event.key === Qt.Key_Alt) || (event.key === Qt.Key_Meta);
        if (isModifierOnly) {
            event.accepted = false;
            return false;
        }
        // Ctrl+Tab/Ctrl+Shift+Tab: Navigate between zones
        var tabKeyPressed = (event.key === 1.67772e+07 || event.key === Qt.Key_Tab);
        var ctrlModifier = !!(event.modifiers & Qt.ControlModifier);
        var shiftModifier = !!(event.modifiers & Qt.ShiftModifier);
        // Only intercept Ctrl+Tab combinations (not plain Tab)
        if (tabKeyPressed && ctrlModifier) {
            event.accepted = true;
            // Ctrl+Shift+Tab: Previous zone
            if (shiftModifier) {
                var currentZoneId = editorController.selectedZoneId || "";
                if (currentZoneId === "" || currentZoneId === null || currentZoneId === undefined) {
                    var zones = editorController.zones;
                    if (zones && zones.length > 0) {
                        var lastZone = zones[zones.length - 1];
                        if (lastZone && lastZone.id) {
                            editorController.selectedZoneId = lastZone.id;
                            return true;
                        }
                    }
                }
                editorController.selectPreviousZone();
                return true;
            }
            // Ctrl+Tab: Next zone
            var currentZoneId = editorController.selectedZoneId || "";
            if (currentZoneId === "" || currentZoneId === null || currentZoneId === undefined) {
                var zones = editorController.zones;
                if (zones && zones.length > 0 && zones[0] && zones[0].id) {
                    editorController.selectedZoneId = zones[0].id;
                    return true;
                }
            }
            editorController.selectNextZone();
            return true;
        }
        // Arrow keys: Move/resize selected zone
        var controllerZoneId = editorController ? (editorController.selectedZoneId || "") : "";
        if (controllerZoneId !== "" && controllerZoneId !== null && controllerZoneId !== undefined) {
            var step = 0.01; // 1% step size
            // Arrow keys without Shift: Move zone(s)
            if (!(event.modifiers & Qt.ShiftModifier)) {
                switch (event.key) {
                case Qt.Key_Left:
                    event.accepted = true;
                    editorController.moveSelectedZones(0, step);
                    return true;
                case Qt.Key_Right:
                    event.accepted = true;
                    editorController.moveSelectedZones(1, step);
                    return true;
                case Qt.Key_Up:
                    event.accepted = true;
                    editorController.moveSelectedZones(2, step);
                    return true;
                case Qt.Key_Down:
                    event.accepted = true;
                    editorController.moveSelectedZones(3, step);
                    return true;
                }
            } else {
                // Shift+Arrow keys: Resize zone(s)
                switch (event.key) {
                case Qt.Key_Left:
                    event.accepted = true;
                    editorController.resizeSelectedZones(0, step);
                    return true;
                case Qt.Key_Right:
                    event.accepted = true;
                    editorController.resizeSelectedZones(1, step);
                    return true;
                case Qt.Key_Up:
                    event.accepted = true;
                    editorController.resizeSelectedZones(2, step);
                    return true;
                case Qt.Key_Down:
                    event.accepted = true;
                    editorController.resizeSelectedZones(3, step);
                    return true;
                }
            }
        }
        event.accepted = false;
        return false;
    }

}
