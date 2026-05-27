// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Doubles as cooperative-B and detached. The transport sets `pinned`
// via PopoutRequest.props so the two instances render with distinct
// chrome and copy. Pinned mode uses a heavier border, a different
// header label, and shifts off center so a simultaneously-open
// cooperative does not stack directly behind it.

import Phosphor.Theme
import QtQuick

Rectangle {
    id: root

    // Set true when this popout is the Detached pinned variant.
    // PopoutRequest.props plumbs this through InAppPopoutTransport's
    // contentItem->setProperty pass, so toggleDetached() in
    // DemoController sets it to true and toggleCooperativeB() leaves
    // it false.
    property bool pinned: false

    implicitWidth: 280
    implicitHeight: 160
    color: Theme.surface_container
    border.color: root.pinned ? Theme.tertiary : Theme.primary
    border.width: root.pinned ? 3 : 1
    radius: Tokens.radius_l

    Column {
        anchors.centerIn: parent
        spacing: Tokens.spacing_s

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.pinned ? qsTr("Pinned Note") : qsTr("Quick Note")
            color: root.pinned ? Theme.tertiary : Theme.on_surface
            font.pixelSize: Tokens.font_size_title_m
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.pinned ? qsTr("Stays open across modal cycles") : qsTr("Pin me with the Detached button")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
        }

    }

}
