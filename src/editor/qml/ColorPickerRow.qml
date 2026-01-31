// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami
import "ColorUtils.js" as ColorUtils

/**
 * @brief Reusable color picker row for zone appearance settings
 *
 * Contains a color preview button and either a hex code label (single mode)
 * or a "Click to set" hint (multi mode).
 *
 * SRP: Single responsibility - color picker UI row.
 */
RowLayout {
    id: colorPickerRow

    /**
     * @brief The base color (without opacity multiplier applied)
     */
    property color baseColor: Qt.transparent

    /**
     * @brief Optional opacity multiplier (0-1) for preview display
     */
    property real opacityMultiplier: 1.0

    /**
     * @brief Whether this is for multi-select mode (shows "Click to set" instead of hex)
     */
    property bool isMultiMode: false

    /**
     * @brief Accessibility name for the color button
     */
    property string accessibleName: ""

    /**
     * @brief Tooltip text for the color button
     */
    property string toolTipText: ""

    /**
     * @brief Size of the color button
     */
    property int buttonSize: Kirigami.Units.gridUnit * 3

    /**
     * @brief Emitted when the color button is clicked
     */
    signal colorButtonClicked()

    spacing: Kirigami.Units.smallSpacing

    // Color preview button - inline Rectangle matching main branch structure
    Rectangle {
        id: colorPreview

        width: colorPickerRow.buttonSize
        height: colorPickerRow.buttonSize
        radius: Kirigami.Units.smallSpacing
        border.color: Kirigami.Theme.disabledTextColor
        border.width: 1
        color: Qt.rgba(
            colorPickerRow.baseColor.r,
            colorPickerRow.baseColor.g,
            colorPickerRow.baseColor.b,
            colorPickerRow.baseColor.a * colorPickerRow.opacityMultiplier
        )
        Accessible.name: colorPickerRow.accessibleName
        ToolTip.text: colorPickerRow.toolTipText
        ToolTip.visible: colorPreviewMouseArea.containsMouse && colorPickerRow.toolTipText !== ""

        MouseArea {
            id: colorPreviewMouseArea
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onClicked: colorPickerRow.colorButtonClicked()
        }
    }

    Label {
        visible: colorPickerRow.isMultiMode
        text: i18nc("@info", "Click to set for all")
        color: Kirigami.Theme.disabledTextColor
        font.pixelSize: Kirigami.Theme.smallFont.pixelSize
    }

    Label {
        visible: !colorPickerRow.isMultiMode
        text: colorPickerRow.baseColor.a > 0 ? colorPickerRow.baseColor.toString().toUpperCase() : ""
        font: Kirigami.Theme.fixedWidthFont
        Accessible.name: i18nc("@info:accessibility", "Color hex code")
    }
}
