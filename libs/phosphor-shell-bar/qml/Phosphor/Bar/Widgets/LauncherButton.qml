// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls.Basic
import Phosphor.Widgets

AbstractButton {
    signal activated
    implicitWidth: 36
    implicitHeight: 34
    Accessible.name: qsTr("Open launcher")
    onClicked: activated()
    contentItem: Item {
        PhosphorMark {
            anchors.centerIn: parent
            width: 32
            height: 32
        }
    }
}
