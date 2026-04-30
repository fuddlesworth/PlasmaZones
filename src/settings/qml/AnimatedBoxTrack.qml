// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Animated box on a track rail, shared between EasingPreview and SpringPreview.
 *
 * Required properties:
 *   - boxSize: int — side length of the animated box
 *
 * Read-only alias:
 *   - animBox: Rectangle — the animated box element (set .x from external timer)
 */
Item {
    id: root

    required property int boxSize
    readonly property alias animBox: animBox

    Accessible.name: i18n("Animation preview track")

    // Track rail
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: root.boxSize / 2
        anchors.rightMargin: root.boxSize / 2
        height: Math.max(2, Math.round(Kirigami.Units.smallSpacing * 0.5))
        radius: height / 2
        color: Kirigami.Theme.disabledTextColor
    }

    // Start marker
    Rectangle {
        x: root.boxSize / 2 - 1
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(1, Math.round(Kirigami.Units.smallSpacing * 0.4))
        height: Kirigami.Units.gridUnit
        radius: 1
        color: Kirigami.Theme.disabledTextColor
        opacity: 0.4
    }

    // End marker
    Rectangle {
        x: parent.width - root.boxSize / 2 - 1
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(1, Math.round(Kirigami.Units.smallSpacing * 0.4))
        height: Kirigami.Units.gridUnit
        radius: 1
        color: Kirigami.Theme.disabledTextColor
        opacity: 0.4
    }

    // Animated box
    Rectangle {
        id: animBox

        width: root.boxSize
        height: root.boxSize
        radius: root.boxSize / 5
        y: (parent.height - height) / 2
        x: 0
        color: Kirigami.Theme.highlightColor
    }

}
