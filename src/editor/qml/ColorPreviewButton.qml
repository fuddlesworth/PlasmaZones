// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import org.kde.kirigami as Kirigami

/**
 * @brief Color preview button with transparency checkerboard
 *
 * Displays a color swatch that can be clicked to open a color picker.
 * Shows a checkerboard pattern behind semi-transparent colors.
 *
 * SRP: Single responsibility - color preview with click interaction.
 */
Rectangle {
    id: colorButton

    /**
     * @brief The color to display (with opacity applied)
     */
    property color displayColor: Qt.transparent

    /**
     * @brief Size of the button (width and height)
     */
    property int buttonSize: Kirigami.Units.gridUnit * 3

    /**
     * @brief Accessibility name for the button
     */
    property string accessibleName: ""

    /**
     * @brief Tooltip text
     */
    property string toolTipText: ""

    /**
     * @brief Emitted when the button is clicked
     */
    signal clicked()

    width: buttonSize
    height: buttonSize
    radius: Kirigami.Units.smallSpacing
    border.color: Kirigami.Theme.disabledTextColor
    border.width: 1
    color: "transparent"  // Let the inner rectangle show the color

    Accessible.name: accessibleName
    ToolTip.text: toolTipText
    ToolTip.visible: mouseArea.containsMouse && toolTipText !== ""

    // Checkerboard for transparency
    TransparencyCheckerboard {
        targetColor: colorButton.displayColor
    }

    // Actual color overlay
    Rectangle {
        anchors.fill: parent
        anchors.margins: Math.round(Kirigami.Units.devicePixelRatio)
        radius: Math.max(0, parent.radius - Math.round(Kirigami.Units.devicePixelRatio))
        color: colorButton.displayColor
    }

    MouseArea {
        id: mouseArea

        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        hoverEnabled: true
        onClicked: colorButton.clicked()
    }
}
