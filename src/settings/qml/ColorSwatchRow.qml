// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief A ColorButton + hex label for color pickers in Kirigami.FormLayout.
 *
 * Displays a color swatch using the existing ColorButton component
 * alongside a fixed-width hex label showing the current color value.
 */
RowLayout {
    // visible is inherited from Item, no need to redeclare

    id: root

    property string formLabel
    property color color

    signal clicked()

    Kirigami.FormData.label: formLabel
    spacing: Kirigami.Units.smallSpacing

    ColorButton {
        color: root.color
        onClicked: root.clicked()
    }

    Label {
        text: root.color.toString().toUpperCase()
        font: Kirigami.Theme.fixedWidthFont
    }

}
