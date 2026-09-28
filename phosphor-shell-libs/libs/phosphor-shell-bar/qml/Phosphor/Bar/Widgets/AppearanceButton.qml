// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme

AbstractButton {
    signal activated
    implicitWidth: 29
    implicitHeight: 29
    Accessible.name: qsTr("Customize shell")
    onClicked: activated()
    contentItem: Text {
        text: "◈"
        color: Appearance.stops[2]
        font.family: Tokens.font_family_ui
        font.pixelSize: 19
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
