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
    //* Name announced to assistive technology for the swatch button; falls
    //* back to the ColorButton's own generic name when unset.
    property string accessibleName: ""

    signal clicked

    // Qt's color.toString() drops the alpha channel when fully opaque
    // (#RRGGBB) but keeps it otherwise (#AARRGGBB), so the label width
    // jumps between 6 and 8 digits and misaligns the rows. Always emit
    // the full 8-digit #AARRGGBB form.
    function _toHexArgb(c) {
        function pad(v) {
            return Math.round(v * 255).toString(16).padStart(2, '0');
        }
        return ("#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)).toUpperCase();
    }

    Kirigami.FormData.label: formLabel
    spacing: Kirigami.Units.smallSpacing

    ColorButton {
        color: root.color
        Accessible.name: root.accessibleName !== "" ? root.accessibleName : i18n("Color picker")
        onClicked: root.clicked()
    }

    Label {
        text: root._toHexArgb(root.color)
        font: Kirigami.Theme.fixedWidthFont
    }
}
