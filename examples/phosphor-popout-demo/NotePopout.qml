// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Doubles as cooperative-B and detached. The transport configures the
// backdrop and dismiss policy from the request; the popout itself is
// generic.

import Phosphor.Theme
import QtQuick

Rectangle {
    id: root

    implicitWidth: 280
    implicitHeight: 160
    color: Theme.surface_container
    border.color: Theme.tertiary
    border.width: 2
    radius: Tokens.radius_l

    Column {
        anchors.centerIn: parent
        spacing: Tokens.spacing_s

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Quick Note")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_title_m
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Pin me with the Detached button")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
        }

    }

}
