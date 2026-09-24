// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Theme

AbstractButton {
    id: root
    property bool on: false
    implicitWidth: 38
    implicitHeight: 23
    Accessible.role: Accessible.CheckBox
    Accessible.checked: on
    background: Rectangle {
        radius: 12
        color: root.on ? Qt.tint(Appearance.card, Qt.alpha(Appearance.stops[0], 0.27)) : Appearance.recess
        border.width: 1
        border.color: root.visualFocus ? Appearance.text : root.on ? Qt.alpha(Appearance.stops[0], 0.55) : Appearance.outline
        Rectangle {
            x: root.on ? 18 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 15
            height: 15
            radius: 8
            color: root.on ? Appearance.stops[0] : Appearance.muted
        }
    }
    opacity: enabled ? 1 : 0.45
}
