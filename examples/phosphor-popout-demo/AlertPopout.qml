// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Modal demo. Heavy backdrop, no click-outside dismiss. Confirms that
// cooperative popouts open while this is up are rejected by the
// controller.

import Phosphor.Theme
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    signal dismissRequested()

    implicitWidth: 360
    implicitHeight: 200
    color: Theme.surface_container_high
    border.color: Theme.error
    border.width: 2
    radius: Tokens.radius_l

    ColumnLayout {
        anchors.centerIn: parent
        spacing: Tokens.spacing_m

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Modal alert")
            color: Theme.on_surface
            font.pixelSize: Tokens.font_size_title_l
            font.family: Tokens.font_family
            font.weight: Tokens.font_weight_medium
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Cooperative popouts are suppressed while this is open.")
            color: Theme.on_surface_variant
            font.pixelSize: Tokens.font_size_body_s
            font.family: Tokens.font_family
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: dismissLabel.implicitWidth + Tokens.spacing_xl
            Layout.preferredHeight: Tokens.spacing_xxl
            color: Theme.error
            radius: Tokens.radius_m

            Text {
                id: dismissLabel

                anchors.centerIn: parent
                text: qsTr("Dismiss")
                color: Theme.on_error
                font.pixelSize: Tokens.font_size_label_l
                font.family: Tokens.font_family
                font.weight: Tokens.font_weight_medium
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: root.dismissRequested()
            }

        }

    }

}
