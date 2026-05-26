// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
// One token card, color swatch + token name + hex.

import Phosphor.Theme
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: root

    required property string tokenName
    required property color tokenColor

    // implicit, not fixed, the parent GridLayout stretches us with
    // Layout.fillWidth so columns share the row evenly. 220 is the
    // intrinsic minimum that still fits the longest token name without
    // eliding.
    implicitWidth: 220
    height: 88
    radius: Tokens.radius_m
    color: Theme.surface_container
    border.color: Theme.outline_variant
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.margins: Tokens.spacing_s
        spacing: Tokens.spacing_m

        // The color sample. Outlined so very-dark surfaces are still
        // distinguishable from the card background.
        Rectangle {
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            radius: Tokens.radius_s
            color: root.tokenColor
            border.color: Theme.outline
            border.width: 1
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0

            Text {
                text: root.tokenName
                color: Theme.on_surface
                font.pixelSize: Tokens.font_size_label_l
                font.weight: Tokens.font_weight_medium
                font.family: Tokens.font_family
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                text: root.tokenColor.toString().toUpperCase()
                color: Theme.on_surface_variant
                font.pixelSize: Tokens.font_size_body_s
                font.family: Tokens.font_family
            }

        }

    }

}
