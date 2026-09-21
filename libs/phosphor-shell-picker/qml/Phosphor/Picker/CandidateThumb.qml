// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Picker.CandidateThumb, one wallpaper candidate in the strip.
//
// A 120 x 68 thumbnail, radius 6, with a 1 px outline that turns into
// the 2 px blue outline while selected (A3 §8e). The thumbnail is the
// only image the picker shows: the preview is the live desktop, so this
// stays small and decodes asynchronously at thumbnail size. Hovering
// reports to the strip, which drives the retint; the strip owns the
// selection.
//
//   CandidateThumb { source: model.path; name: model.name; selected: ... }

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: thumb

    property url source: ""
    property string name: ""
    property bool selected: false
    property bool current: false

    signal hoveredChanged2(bool hovered)
    signal clicked
    signal doubleClicked

    readonly property bool hovered: hover.hovered

    implicitWidth: 120
    implicitHeight: 68

    Accessible.role: Accessible.ListItem
    Accessible.name: thumb.current ? qsTr("%1 (current wallpaper)").arg(thumb.name) : thumb.name
    Accessible.selected: thumb.selected

    Rectangle {
        id: frame

        anchors.fill: parent
        radius: Tokens.radius_edge
        color: Theme.surface_variant
        clip: true

        Image {
            anchors.fill: parent
            source: thumb.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            // Decoded at twice the thumbnail for HiDPI, never at the
            // wallpaper's full size.
            sourceSize: Qt.size(240, 136)
        }

        // The current wallpaper carries a small cyan mark on its bottom
        // edge, so the strip says where it starts from.
        SpectrumUnderline {
            visible: thumb.current
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            length: 24
            t: 0
        }
    }

    // Resting: 1 px outline. Selected: 2 px blue. Hovered: the stroke
    // lights up, never a fill (05 hover rule).
    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_edge
        color: "transparent"
        border.width: thumb.selected ? 2 : 1
        border.color: thumb.selected ? Spectrum.active : Theme.outline
        opacity: thumb.selected ? 1 : (thumb.hovered ? Tokens.stroke_active : Tokens.stroke_resting)

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_enter_content
                easing: Motion.reveal
            }
        }
    }

    HoverHandler {
        id: hover

        onHoveredChanged: thumb.hoveredChanged2(hovered)
    }

    TapHandler {
        onTapped: thumb.clicked()
        onDoubleTapped: thumb.doubleClicked()
    }
}
