// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.SpectrumUnderline, the 2 px line that IS the control.
//
// A chip underline, a slider track, an on/off light, the flash under a
// changed digit: one 2 px rectangle in the ramp colour at `t` (or an
// explicit `color`). As a slider, `value` scales its width to a fraction
// of `length`. `tick()` runs the tick primitive: the line pops to full
// opacity in 60 ms with overshoot and then releases back to
// `restOpacity`, retargeting from wherever it currently is (R6).
//
//   SpectrumUnderline { anchors.bottom: parent.bottom; t: chip.t }
//   SpectrumUnderline { length: track.width; value: volume }

import QtQuick
import Phosphor.Theme

Rectangle {
    id: root

    property real t: 0
    // Full extent when `value` is 1. Defaults to the parent's width so a
    // bare underline spans its host.
    property real length: parent ? parent.width : 0
    // 0..1 fraction of `length` on display (slider fill).
    property real value: 1
    // Opacity the line returns to after a tick. A digit underline rests
    // hidden (0); a chip underline or slider rests visible (1).
    property real restOpacity: 1

    implicitHeight: 2
    height: 2
    width: length * Math.max(0, Math.min(1, value))
    color: Spectrum.at(t)

    // Opacity is owned by the tick animation, not a binding, so the
    // animation's writes are never snapped back by a re-evaluation.
    Component.onCompleted: opacity = restOpacity
    onRestOpacityChanged: {
        if (!tickAnimation.running)
            opacity = restOpacity;
    }

    function tick() {
        tickAnimation.restart();
    }

    Behavior on width {
        NumberAnimation {
            duration: Motion.duration_release
            easing: Motion.release
        }
    }

    SequentialAnimation {
        id: tickAnimation

        NumberAnimation {
            target: root
            property: "opacity"
            to: 1
            duration: Motion.duration_tick
            easing: Motion.reducedMotion ? Motion.reveal : Motion.tick
        }
        NumberAnimation {
            target: root
            property: "opacity"
            to: root.restOpacity
            duration: Motion.duration_release
            easing: Motion.release
        }
    }
}
