// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Rectangle {
    property alias text: label.text
    implicitWidth: label.implicitWidth + 10
    implicitHeight: 20
    radius: 4
    color: "transparent"
    border.width: 1
    border.color: Appearance.outline
    Text {
        id: label
        anchors.centerIn: parent
        font.family: Tokens.font_family_mono
        font.pixelSize: 9
        color: Appearance.muted
    }
}
