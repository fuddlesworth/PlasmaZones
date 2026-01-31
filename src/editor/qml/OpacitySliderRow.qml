// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable opacity slider row with percentage label
 *
 * Contains a slider (0-100) and a percentage label showing the current value.
 *
 * SRP: Single responsibility - opacity slider UI row.
 */
RowLayout {
    id: opacitySliderRow

    /**
     * @brief Current opacity value (0-1 scale, displayed as 0-100%)
     */
    property real opacityValue: 0.5

    /**
     * @brief Default opacity value (0-1 scale) used when opacityValue is undefined
     */
    property real defaultOpacity: 0.5

    /**
     * @brief Whether the slider is enabled
     */
    property bool sliderEnabled: true

    /**
     * @brief Accessibility name for the slider
     */
    property string accessibleName: ""

    /**
     * @brief Tooltip text for the slider
     */
    property string toolTipText: ""

    /**
     * @brief Emitted when the user moves the slider (not on programmatic changes)
     * @param value The new opacity value (0-1 scale)
     * Note: Named 'opacityEdited' to avoid conflict with Item.opacity's built-in opacityChanged signal
     */
    signal opacityEdited(real value)

    spacing: Kirigami.Units.smallSpacing

    Slider {
        id: slider

        Layout.fillWidth: true
        from: 0
        to: 100
        stepSize: 1
        value: opacitySliderRow.opacityValue * 100
        enabled: opacitySliderRow.sliderEnabled
        Accessible.name: opacitySliderRow.accessibleName
        ToolTip.text: opacitySliderRow.toolTipText
        ToolTip.visible: hovered && opacitySliderRow.toolTipText !== ""

        onMoved: {
            Qt.callLater(function() {
                opacitySliderRow.opacityEdited(slider.value / 100);
            });
        }
    }

    Label {
        text: Math.round(slider.value) + "%"
        Layout.preferredWidth: 40
        horizontalAlignment: Text.AlignRight
        font: Kirigami.Theme.fixedWidthFont
    }
}
