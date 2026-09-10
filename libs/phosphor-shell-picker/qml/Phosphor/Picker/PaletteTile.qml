// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Picker.PaletteTile, one theme preset as a five-swatch strip.
//
// The theme view of the picker draws palettes as tiles without images
// (A3 §8b): five swatches (surface, container, primary, secondary,
// tertiary) side by side under a 6 px radius, the preset's name under
// them. Same outline rules as CandidateThumb.
//
//   PaletteTile { name: model.name; swatches: model.swatches; selected: ... }

import QtQuick
import Phosphor.Theme

Item {
    id: tile

    property string name: ""
    // Five colours, from ThemePresets.swatchesFor.
    property var swatches: []
    property bool selected: false

    signal hoveredChanged2(bool hovered)
    signal clicked
    signal doubleClicked

    readonly property bool hovered: hover.hovered

    implicitWidth: 96
    implicitHeight: 68

    Accessible.role: Accessible.ListItem
    Accessible.name: tile.name
    Accessible.selected: tile.selected

    Rectangle {
        id: strip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 46
        radius: Tokens.radius_edge
        color: Theme.surface_variant
        clip: true

        Row {
            anchors.fill: parent

            Repeater {
                model: tile.swatches

                Rectangle {
                    required property var modelData

                    width: strip.width / Math.max(1, tile.swatches.length)
                    height: strip.height
                    color: modelData
                }
            }
        }
    }

    Rectangle {
        anchors.fill: strip
        radius: Tokens.radius_edge
        color: "transparent"
        border.width: tile.selected ? 2 : 1
        border.color: tile.selected ? Spectrum.active : Theme.outline
        opacity: tile.selected ? 1 : (tile.hovered ? Tokens.stroke_active : Tokens.stroke_resting)

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: strip.bottom
        anchors.topMargin: Tokens.spacing_xs
        text: tile.name
        color: tile.selected ? Theme.on_surface : Theme.on_surface_variant
        font.family: Tokens.font_family_ui
        font.pixelSize: Tokens.font_size_label_s
        elide: Text.ElideRight
    }

    HoverHandler {
        id: hover

        onHoveredChanged: tile.hoveredChanged2(hovered)
    }

    TapHandler {
        onTapped: tile.clicked()
        onDoubleTapped: tile.doubleClicked()
    }
}
