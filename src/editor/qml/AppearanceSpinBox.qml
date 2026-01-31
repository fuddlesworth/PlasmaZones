// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable spinbox for appearance settings (border width, radius, etc.)
 *
 * Wraps SpinBox with common configuration and emits valueModified signal.
 *
 * SRP: Single responsibility - numeric input for appearance properties.
 */
SpinBox {
    id: appearanceSpinBox

    /**
     * @brief Current value
     */
    property int spinValue: 0

    /**
     * @brief Default value used when spinValue is undefined
     */
    property int defaultValue: 0

    /**
     * @brief Whether the spinbox is enabled
     */
    property bool spinEnabled: true

    /**
     * @brief Accessibility name
     */
    property string accessibleName: ""

    /**
     * @brief Tooltip text
     */
    property string toolTipText: ""

    /**
     * @brief Emitted when the value is modified by user
     * @param newValue The new value
     */
    signal spinValueModified(int newValue)

    value: (spinValue !== undefined) ? spinValue : defaultValue
    enabled: spinEnabled
    Accessible.name: accessibleName
    ToolTip.text: toolTipText
    ToolTip.visible: hovered && toolTipText !== ""

    onValueModified: {
        appearanceSpinBox.spinValueModified(value);
    }
}
