// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.SpectrumStroke, the 1 px inset edge of every surface.
//
// Depth on chrome is a stroke, not a shadow (05 R2). Lay this over any
// surface and it draws a 1 px border in the ramp colour at `t`, resting
// at Tokens.stroke_resting and rising to stroke_active while `active`:
// the rise is the enter primitive, the fall the release, so hover and
// press read as the stroke lighting up rather than a filled overlay.
// `focused` lays a white edge over it, because focus is white and never a
// hue (R9).
//
// `t` defaults to the rail axis of the surface's screen position when
// `screenX` / `screenWidth` are given; bind `t` directly for the structure
// or state axis.
//
//   SpectrumStroke { anchors.fill: parent; radius: Tokens.radius_edge; screenX: root.x; screenWidth: Screen.width; active: hover.hovered }

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property real radius: 0
    // Rail-axis inputs; only read by the default `t` binding.
    property real screenX: 0
    property real screenWidth: 0
    property real t: Spectrum.tForX(screenX, screenWidth)
    property bool active: false
    property bool focused: false

    readonly property real _restOpacity: Theme.isDark ? Tokens.stroke_resting : Tokens.stroke_resting_light

    Rectangle {
        id: stroke

        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: Spectrum.at(root.t)
        opacity: root.active ? Tokens.stroke_active : root._restOpacity

        Behavior on opacity {
            NumberAnimation {
                duration: root.active ? Motion.duration_enter : Motion.duration_release
                // Enter carries a hair of overshoot, which reduced motion
                // drops for the plain reveal curve.
                easing: root.active ? (Motion.reducedMotion ? Motion.reveal : Motion.enter) : Motion.release
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: Spectrum.focus
        opacity: root.focused ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: root.focused ? Motion.duration_enter : Motion.duration_release
                easing: root.focused ? Motion.reveal : Motion.release
            }
        }
    }
}
