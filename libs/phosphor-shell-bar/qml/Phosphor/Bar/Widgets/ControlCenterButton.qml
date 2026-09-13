// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

ShellButton {
    id: root
    property real railT: 0.93
    signal activated
    iconName: "configure"
    label: qsTr("Quick settings")
    implicitWidth: 34
    implicitHeight: 32
    highlighted: true
    onClicked: root.activated()
}
