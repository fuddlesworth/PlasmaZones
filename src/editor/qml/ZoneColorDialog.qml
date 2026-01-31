// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Dialogs
import "ColorUtils.js" as ColorUtils

/**
 * @brief Reusable color dialog for zone color selection
 *
 * Wraps ColorDialog with ARGB conversion and optional auto-update from zone changes.
 *
 * SRP: Single responsibility - color selection dialog with ARGB support.
 */
ColorDialog {
    id: zoneColorDialog

    /**
     * @brief The editor controller for updates
     */
    property var editorController: null

    /**
     * @brief The selected zone ID (for single mode)
     */
    property string selectedZoneId: ""

    /**
     * @brief The selected zone object (for reading current color)
     */
    property var selectedZone: null

    /**
     * @brief The color property name (e.g., "highlightColor", "inactiveColor", "borderColor")
     */
    property string colorProperty: ""

    /**
     * @brief Whether this is for multi-select mode
     */
    property bool isMultiMode: false

    /**
     * @brief Emitted when a color is accepted (with ARGB hex string)
     * @param hexColor The color in #AARRGGBB format
     */
    signal colorAccepted(string hexColor)

    /**
     * @brief Emitted when multi-mode color is accepted
     * @param hexColor The color in #AARRGGBB format
     * @param selectedColor The Qt color object for preview updates
     */
    signal multiColorAccepted(string hexColor, color selectedColor)

    onAccepted: {
        var hexColor = ColorUtils.colorToArgbHex(selectedColor);

        if (isMultiMode) {
            multiColorAccepted(hexColor, selectedColor);
        } else {
            colorAccepted(hexColor);
        }
    }

    // Auto-update selectedColor when zone color changes (single mode only)
    Connections {
        target: zoneColorDialog.editorController
        enabled: !zoneColorDialog.isMultiMode &&
                 zoneColorDialog.editorController !== null &&
                 zoneColorDialog.selectedZoneId !== "" &&
                 zoneColorDialog.colorProperty !== ""

        function onZoneColorChanged(zoneId) {
            if (zoneId === zoneColorDialog.selectedZoneId &&
                zoneColorDialog.selectedZone &&
                zoneColorDialog.selectedZone[zoneColorDialog.colorProperty] &&
                !zoneColorDialog.visible) {

                var colorValue = zoneColorDialog.selectedZone[zoneColorDialog.colorProperty];
                zoneColorDialog.selectedColor = (typeof colorValue === 'string')
                    ? ColorUtils.parseArgbHex(colorValue)
                    : colorValue;
            }
        }
    }
}
