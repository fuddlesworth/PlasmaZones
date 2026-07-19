// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

/**
 * A single key cap ("Ctrl", "Shift", "F9") rendered as a bordered chip.
 * Used by CheatsheetContent to display key sequences one token per chip.
 */
Rectangle {
    id: root

    /// The key token to display.
    property alias text: keyLabel.text

    implicitWidth: Math.max(keyLabel.implicitWidth + Kirigami.Units.smallSpacing * 2, implicitHeight)
    implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
    radius: Kirigami.Units.smallSpacing
    color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
    border.width: 1
    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.25)

    Label {
        id: keyLabel

        anchors.centerIn: parent
        font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.9)
        color: Kirigami.Theme.textColor
    }
}
