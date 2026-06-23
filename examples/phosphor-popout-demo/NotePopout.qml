// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Doubles as cooperative-B and detached. The transport sets `pinned`
// via PopoutRequest.props so the two instances render with distinct
// chrome and copy. Pinned mode uses a heavier border and a different
// header label so a simultaneously-open cooperative is visually
// distinct from the pinned variant.

import Phosphor.Theme
import QtQuick
import QtQuick.Layouts

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
    Accessible.role: Accessible.Dialog
    Accessible.name: root.pinned ? qsTr("Pinned note popout") : qsTr("Quick note popout")
    // Distinct semantic role information for AT users beyond what the
    // body Text already conveys to sighted users — describing the
    // exclusivity behaviour (detached vs cooperative) rather than
    // repeating the visible copy verbatim.
    Accessible.description: root.pinned ? qsTr("Detached popout; persists across modal cycles") : qsTr("Cooperative popout; dismissed when another cooperative or a modal opens")

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Tokens.spacing_s

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.pinned ? qsTr("Pinned Note") : qsTr("Quick Note")
            color: root.pinned ? Theme.tertiary : Theme.on_surface
            font.pixelSize: Tokens.font_size_title_m
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.pinned ? qsTr("Stays open across modal cycles") : qsTr("Pin me with the Detached button")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
        }
    }
}
