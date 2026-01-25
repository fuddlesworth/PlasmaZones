// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Grid overlay for snapping visualization
 *
 * Draws grid lines based on snap intervals to help with zone positioning.
 * Extracted from EditorWindow.qml to reduce file size.
 */
Canvas {
    id: gridOverlay

    // Required references
    required property var editorController

    anchors.fill: parent
    visible: editorController ? (editorController.gridOverlayVisible && editorController.gridSnappingEnabled) : false
    opacity: 0.25
    onPaint: {
        // Validate dimensions before drawing
        if (width <= 0 || height <= 0 || !isFinite(width) || !isFinite(height))
            return ;

        var ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);
        ctx.strokeStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.3);
        ctx.lineWidth = 1;
        var intervalX = editorController ? editorController.snapIntervalX : 0.1;
        var intervalY = editorController ? editorController.snapIntervalY : 0.1;
        // Validate intervals (1% to 50% range)
        if (!isFinite(intervalX) || intervalX < 0.01 || intervalX > 0.5)
            intervalX = 0.1;

        if (!isFinite(intervalY) || intervalY < 0.01 || intervalY > 0.5)
            intervalY = 0.1;

        var stepX = width * intervalX;
        var stepY = height * intervalY;
        // Validate step sizes
        if (stepX <= 0 || !isFinite(stepX) || stepY <= 0 || !isFinite(stepY))
            return ;

        ctx.beginPath();
        // Draw vertical lines - edge to edge (from 0 to width)
        for (var x = 0; x <= width; x += stepX) {
            // Round to nearest pixel for crisp lines
            var roundedX = Math.round(x);
            // Clamp to canvas bounds to ensure lines are drawn edge-to-edge
            roundedX = Math.max(0, Math.min(width, roundedX));
            ctx.moveTo(roundedX, 0);
            ctx.lineTo(roundedX, height);
        }
        // Draw horizontal lines - edge to edge (from 0 to height)
        for (var y = 0; y <= height; y += stepY) {
            // Round to nearest pixel for crisp lines
            var roundedY = Math.round(y);
            // Clamp to canvas bounds to ensure lines are drawn edge-to-edge
            roundedY = Math.max(0, Math.min(height, roundedY));
            ctx.moveTo(0, roundedY);
            ctx.lineTo(width, roundedY);
        }
        ctx.stroke();
    }
    // Repaint when canvas size changes
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Component.onCompleted: requestPaint()

    Connections {
        function onSnapIntervalXChanged() {
            gridOverlay.requestPaint();
        }

        function onSnapIntervalYChanged() {
            gridOverlay.requestPaint();
        }

        function onSnapIntervalChanged() {
            gridOverlay.requestPaint();
        }

        function onGridSnappingEnabledChanged() {
            gridOverlay.requestPaint();
        }

        function onGridOverlayVisibleChanged() {
            gridOverlay.requestPaint();
        }

        target: gridOverlay.editorController
        enabled: gridOverlay.editorController !== null
    }

}
