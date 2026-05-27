// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// Modal demo. Heavy backdrop, no click-outside dismiss. Confirms that
// cooperative popouts open while this is up are rejected by the
// controller.

import Phosphor.PopoutDemo
import Phosphor.Theme
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    // The transport injects a back-reference to the PopoutHost via a
    // dynamic property named `_popoutHost`. The Dismiss button calls
    // its `dismiss()` function. Without this, the modal cannot close
    // itself because dismissOnClickOutside is disabled for modals.
    property QtObject _popoutHost: null

    implicitWidth: 360
    implicitHeight: 200
    color: Theme.surface_container_high
    border.color: Theme.error
    border.width: 2
    radius: Tokens.radius_l
    Accessible.role: Accessible.AlertMessage
    Accessible.name: qsTr("Modal alert")

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

        PhosphorButton {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Dismiss")
            accentColor: Theme.error
            labelColor: Theme.on_error
            onClicked: {
                if (root._popoutHost)
                    root._popoutHost.dismiss();

            }
        }

    }

}
