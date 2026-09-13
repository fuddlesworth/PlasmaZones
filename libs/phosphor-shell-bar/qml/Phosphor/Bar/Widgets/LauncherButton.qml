// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme

AbstractButton {
    signal activated
    implicitWidth: 36
    implicitHeight: 34
    Accessible.name: qsTr("Open launcher")
    onClicked: activated()
    contentItem: Text {
        text: "φ"
        color: Appearance.stops[0]
        font.family: Tokens.font_family_ui
        font.pixelSize: 24
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
