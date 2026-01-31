// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Checkerboard pattern for transparency preview
 *
 * Displays a standard checkerboard pattern behind semi-transparent colors
 * to visualize the transparency level.
 *
 * SRP: Single responsibility - only draws checkerboard pattern.
 */
Canvas {
    id: checkerboard

    /**
     * @brief The color being previewed (used to determine visibility)
     */
    property color targetColor: Qt.transparent

    /**
     * @brief Size of each checkerboard square in pixels
     */
    property int squareSize: 4

    anchors.fill: parent
    anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
    visible: targetColor.a < 1

    onPaint: {
        var ctx = getContext("2d");
        var size = squareSize;
        // Use theme-neutral colors for checkerboard pattern (standard for transparency preview)
        var lightGray = Qt.rgba(
            Kirigami.Theme.disabledTextColor.r,
            Kirigami.Theme.disabledTextColor.g,
            Kirigami.Theme.disabledTextColor.b,
            0.2
        );
        var white = Qt.rgba(1, 1, 1, 1);

        for (var x = 0; x < width; x += size) {
            for (var y = 0; y < height; y += size) {
                ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white;
                ctx.fillRect(x, y, size, size);
            }
        }
    }

    // Repaint when size changes
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
}
