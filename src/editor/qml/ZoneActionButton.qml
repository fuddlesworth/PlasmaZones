// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * @brief Reusable action button for zone operations
 *
 * Provides a themed rectangle button with icon, hover animations,
 * and accessibility support. Used by ActionButtons.qml.
 */
Rectangle {
    id: actionButton

    required property string iconSource
    required property string accessibleName
    required property string accessibleDescription
    required property string tooltipText
    property bool useNegativeColor: false
    property alias containsMouse: buttonMouseArea.containsMouse

    signal activated()

    width: Kirigami.Units.gridUnit * 2.5
    height: Kirigami.Units.gridUnit * 2.5
    radius: Kirigami.Units.smallSpacing * 1.5
    color: {
        if (useNegativeColor && buttonMouseArea.containsMouse)
            return Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.5);

        return buttonMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15);
    }
    border.width: Math.round(Kirigami.Units.devicePixelRatio)
    border.color: {
        if (useNegativeColor && buttonMouseArea.containsMouse)
            return Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.5);

        return buttonMouseArea.containsMouse ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.4) : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08);
    }
    z: 101

    Kirigami.Icon {
        anchors.centerIn: parent
        source: actionButton.iconSource
        width: Kirigami.Units.iconSizes.smallMedium
        height: Kirigami.Units.iconSizes.smallMedium
    }

    MouseArea {
        id: buttonMouseArea

        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        propagateComposedEvents: false
        z: 102
        acceptedButtons: Qt.LeftButton
        Accessible.role: Accessible.Button
        Accessible.name: actionButton.accessibleName
        Accessible.description: actionButton.accessibleDescription
        onPressed: function(mouse) {
            mouse.accepted = true;
        }
        onClicked: function(mouse) {
            mouse.accepted = true;
            actionButton.activated();
        }
    }

    ToolTip {
        visible: buttonMouseArea.containsMouse
        text: actionButton.tooltipText
    }

    Behavior on color {
        ColorAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }

    }

    Behavior on border.color {
        ColorAnimation {
            duration: 200
            easing.type: Easing.OutCubic
        }

    }

}
