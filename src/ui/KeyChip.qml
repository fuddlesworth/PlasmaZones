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
    /// User font settings, forwarded by the host so the caps track the
    /// row labels (same family, same scale) instead of diverging from
    /// them. The 0.9 factor keeps caps slightly compact relative to the
    /// labels at every scale.
    property string fontFamily: ""
    property real fontSizeScale: 1

    implicitWidth: Math.max(keyLabel.implicitWidth + Kirigami.Units.smallSpacing * 2, implicitHeight)
    implicitHeight: keyLabel.implicitHeight + Kirigami.Units.smallSpacing
    radius: Kirigami.Units.smallSpacing
    color: Qt.alpha(Kirigami.Theme.textColor, 0.08)
    border.width: 1
    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.25)

    Label {
        id: keyLabel

        anchors.centerIn: parent
        font.family: root.fontFamily.length > 0 ? root.fontFamily : Kirigami.Theme.defaultFont.family
        font.pixelSize: Math.round(Kirigami.Theme.defaultFont.pixelSize * 0.9 * root.fontSizeScale)
        color: Kirigami.Theme.textColor
    }
}
