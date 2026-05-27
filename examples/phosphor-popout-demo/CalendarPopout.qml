// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Cooperative-scope popout. Click outside dismisses. Opening a
// sibling cooperative in the same scope swaps it out.

import Phosphor.Theme
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    implicitWidth: 320
    implicitHeight: 220
    color: Theme.surface_container_high
    border.color: Theme.outline_variant
    border.width: 1
    radius: Tokens.radius_l
    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Calendar popout")

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Tokens.spacing_s

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Calendar")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_title_l
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Cooperative scope: default")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Click outside to dismiss")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_label_m
            font.family: Tokens.font_family
        }

    }

}
