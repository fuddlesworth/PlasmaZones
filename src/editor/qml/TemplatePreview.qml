// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Visual preview of a template layout pattern
 *
 * Renders a small thumbnail showing the zone layout pattern for template types.
 * Used in ComboBox dropdowns to show what each template looks like.
 */
Canvas {
    // 40px

    id: root

    // Template type: "grid", "columns", "rows", "priority", "focus"
    property string templateType: ""
    property int columns: 2
    property int rows: 2
    // Dimensions using Kirigami.Units for consistency
    readonly property int previewPadding: Kirigami.Units.smallSpacing / 2
    // 2px padding
    readonly property int previewWidth: Kirigami.Units.gridUnit * 7.5
    // 60px (3:2 aspect ratio)
    readonly property int previewHeight: Kirigami.Units.gridUnit * 5

    implicitWidth: previewWidth
    implicitHeight: previewHeight
    onPaint: {
        if (width <= 0 || height <= 0 || !isFinite(width) || !isFinite(height))
            return ;

        var ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);
        // Colors using theme
        var zoneColor = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6);
        var borderColor = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.8);
        ctx.fillStyle = zoneColor;
        ctx.strokeStyle = borderColor;
        ctx.lineWidth = 1;
        var padding = previewPadding;
        var w = width - padding * 2;
        var h = height - padding * 2;
        var offsetX = padding;
        var offsetY = padding;
        if (templateType === "grid") {
            // Draw grid zones
            var colWidth = w / columns;
            var rowHeight = h / rows;
            for (var row = 0; row < rows; row++) {
                for (var col = 0; col < columns; col++) {
                    var x = offsetX + col * colWidth;
                    var y = offsetY + row * rowHeight;
                    ctx.fillRect(x, y, colWidth, rowHeight);
                    ctx.strokeRect(x, y, colWidth, rowHeight);
                }
            }
        } else if (templateType === "columns") {
            // Draw column zones
            var colWidth = w / columns;
            for (var col = 0; col < columns; col++) {
                var x = offsetX + col * colWidth;
                ctx.fillRect(x, offsetY, colWidth, h);
                ctx.strokeRect(x, offsetY, colWidth, h);
            }
        } else if (templateType === "rows") {
            // Draw row zones
            var rowHeight = h / rows;
            for (var row = 0; row < rows; row++) {
                var y = offsetY + row * rowHeight;
                ctx.fillRect(offsetX, y, w, rowHeight);
                ctx.strokeRect(offsetX, y, w, rowHeight);
            }
        } else if (templateType === "priority") {
            // Priority grid: large left (2/3), two small right (1/3 each, stacked)
            var leftWidth = w * 0.67;
            var rightWidth = w * 0.33;
            var rightHeight = h / 2;
            // Left zone
            ctx.fillRect(offsetX, offsetY, leftWidth, h);
            ctx.strokeRect(offsetX, offsetY, leftWidth, h);
            // Top right
            ctx.fillRect(offsetX + leftWidth, offsetY, rightWidth, rightHeight);
            ctx.strokeRect(offsetX + leftWidth, offsetY, rightWidth, rightHeight);
            // Bottom right
            ctx.fillRect(offsetX + leftWidth, offsetY + rightHeight, rightWidth, rightHeight);
            ctx.strokeRect(offsetX + leftWidth, offsetY + rightHeight, rightWidth, rightHeight);
        } else if (templateType === "focus") {
            // Focus: left panel (15%), center (70%), right panel (15%)
            var leftWidth = w * 0.15;
            var centerWidth = w * 0.7;
            var rightWidth = w * 0.15;
            // Left panel
            ctx.fillRect(offsetX, offsetY, leftWidth, h);
            ctx.strokeRect(offsetX, offsetY, leftWidth, h);
            // Center
            ctx.fillRect(offsetX + leftWidth, offsetY, centerWidth, h);
            ctx.strokeRect(offsetX + leftWidth, offsetY, centerWidth, h);
            // Right panel
            ctx.fillRect(offsetX + leftWidth + centerWidth, offsetY, rightWidth, h);
            ctx.strokeRect(offsetX + leftWidth + centerWidth, offsetY, rightWidth, h);
        }
        ctx.stroke();
    }
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Component.onCompleted: requestPaint()
}
