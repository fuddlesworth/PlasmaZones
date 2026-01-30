// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief A color button that shows a color swatch with transparency support
 */
Rectangle {
    id: root

    property int buttonSize: 32

    width: buttonSize
    height: buttonSize
    radius: Kirigami.Units.smallSpacing
    border.color: Kirigami.Theme.disabledTextColor
    border.width: 1

    signal clicked()

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }

    // Checkerboard pattern for transparency preview
    Canvas {
        anchors.fill: parent
        anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
        visible: root.color.a < 1.0

        onPaint: {
            var ctx = getContext("2d")
            var size = 4
            // Use theme-neutral colors for checkerboard pattern (standard for transparency preview)
            var lightGray = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2)
            var white = Qt.rgba(1.0, 1.0, 1.0, 1.0)
            for (var x = 0; x < width; x += size) {
                for (var y = 0; y < height; y += size) {
                    ctx.fillStyle = ((x / size + y / size) % 2 === 0) ? lightGray : white
                    ctx.fillRect(x, y, size, size)
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
        radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
        color: root.color
    }
}
