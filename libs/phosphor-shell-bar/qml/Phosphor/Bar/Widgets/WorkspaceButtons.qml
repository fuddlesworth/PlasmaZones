// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import QtQuick.Controls
import Phosphor.Theme
import Phosphor.Shell

Row {
    spacing: 4
    Repeater {
        model: Workspaces.model
        AbstractButton {
            required property string workspaceId
            required property string name
            required property bool isActive
            required property int index
            width: 24
            height: 25
            Accessible.name: qsTr("Switch to %1").arg(name)
            onClicked: Workspaces.switchTo(workspaceId)
            contentItem: Text {
                text: String(parent.index + 1).padStart(2, "0")
                color: parent.isActive ? Appearance.text : Appearance.muted
                font.family: Tokens.font_family_mono
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                radius: 5
                color: parent.isActive ? Appearance.card : "transparent"
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 2
                    radius: 1
                    color: Appearance.stops[1]
                    visible: parent.parent.isActive
                }
            }
        }
    }
}
