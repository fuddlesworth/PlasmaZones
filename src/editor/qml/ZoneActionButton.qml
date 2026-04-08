// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import "ThemeHelpers.js" as Theme
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable action button for zone operations
 *
 * Provides a themed button with icon, hover animations, keyboard support,
 * and accessibility. Used by ActionButtons.qml.
 */
AbstractButton {
    id: actionButton

    required property string iconSource
    required property string accessibleName
    required property string accessibleDescription
    required property string tooltipText
    required property real buttonSize
    property bool useNegativeColor: false

    width: buttonSize
    height: buttonSize
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    z: 101
    Accessible.role: Accessible.Button
    Accessible.name: actionButton.accessibleName
    Accessible.description: actionButton.accessibleDescription
    ToolTip.text: actionButton.tooltipText
    ToolTip.visible: hovered

    contentItem: Kirigami.Icon {
        source: actionButton.iconSource
        width: Kirigami.Units.iconSizes.smallMedium
        height: Kirigami.Units.iconSizes.smallMedium
    }

    background: Rectangle {
        radius: Kirigami.Units.smallSpacing * Theme.radiusMultiplier
        color: {
            if (actionButton.useNegativeColor && actionButton.hovered)
                return Theme.withAlpha(Kirigami.Theme.negativeTextColor, 0.5);

            return actionButton.hovered ? Theme.withAlpha(Kirigami.Theme.textColor, 0.4) : Theme.withAlpha(Kirigami.Theme.textColor, 0.15);
        }
        border.width: actionButton.activeFocus ? Math.round(Kirigami.Units.devicePixelRatio * Theme.focusBorderWidth) : Math.round(Kirigami.Units.devicePixelRatio)
        border.color: {
            if (actionButton.activeFocus)
                return Theme.withAlpha(Kirigami.Theme.highlightColor, 0.8);

            if (actionButton.useNegativeColor && actionButton.hovered)
                return Theme.withAlpha(Kirigami.Theme.negativeTextColor, 0.5);

            return actionButton.hovered ? Theme.withAlpha(Kirigami.Theme.highlightColor, 0.4) : Theme.withAlpha(Kirigami.Theme.textColor, 0.08);
        }

        Behavior on border.width {
            NumberAnimation {
                duration: Theme.animDuration
                easing.type: Theme.animEasing
            }

        }

        Behavior on color {
            ColorAnimation {
                duration: Theme.animDuration
                easing.type: Theme.animEasing
            }

        }

        Behavior on border.color {
            ColorAnimation {
                duration: Theme.animDuration
                easing.type: Theme.animEasing
            }

        }

    }

}
