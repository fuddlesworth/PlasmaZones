// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PhosphorRipple, M3 state layer + touch ripple.
//
// Drop this in as the interactive top layer of any control: it owns the
// hover / press state-layer overlay (opacities from Phosphor.Theme's
// StateLayer singleton) and the expanding press ripple, and re-emits the
// tap as `tapped()`. The host control wires `tapped` to its own
// `clicked` signal and reads `down` / `hovered` if it needs them.
//
//   Rectangle {
//       radius: height / 2
//       PhosphorRipple {
//           anchors.fill: parent
//           radius: parent.radius
//           rippleColor: someContentColor
//           onTapped: control.clicked()
//       }
//   }
//
// Known limitation: Item.clip is rectangular, so the expanding ripple
// circle is not masked to `radius`. On a pill or rounded surface the
// circle can momentarily bleed past the rounded corners while it
// expands. The state-layer overlay below honours `radius`, so the
// resting hover / press tint is correctly rounded; only the transient
// ripple sweep is unclipped. A rounded mask lands when the connected-
// corner work (Phase 3.2) brings a shared clip primitive.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    // Foreground colour for both the state-layer overlay and the ripple.
    // The host passes its resolved content colour so the overlay reads
    // correctly on any container.
    property color rippleColor: Theme.on_surface
    // Gate for the whole interaction. A disabled host sets this false:
    // no hover tint, no ripple, no tap.
    property bool interactive: true
    // Corner radius for the resting state-layer overlay. Match the host
    // container's radius so the hover / press tint is rounded.
    property real radius: 0

    // Live interaction state for hosts that tint other chrome on hover
    // or press (e.g. an elevation bump on a pressed button).
    readonly property bool hovered: hover.hovered
    readonly property bool down: tap.pressed

    // Re-emitted tap. Hosts connect this to their own clicked signal so
    // any future ripple-side logic (focus restore, haptics) routes
    // through one place.
    signal tapped

    clip: true

    HoverHandler {
        id: hover

        enabled: root.interactive
    }

    // Resting state layer: hover / press tint painted over the host.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.rippleColor
        opacity: {
            if (!root.interactive)
                return 0;
            if (tap.pressed)
                return StateLayer.pressed;
            if (hover.hovered)
                return StateLayer.hover;
            return 0;
        }

        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_short_2
                easing: Motion.standard
            }
        }
    }

    // Expanding press ripple. Centred on the press point, grows to cover
    // the host, fades as it goes.
    Rectangle {
        id: circle

        // Press-point centre, set by start() before each animation.
        property real cx: 0
        property real cy: 0
        // Diameter that guarantees coverage from any press point: twice
        // the host's diagonal.
        readonly property real maxDiameter: 2 * Math.sqrt(root.width * root.width + root.height * root.height)

        width: 0
        height: width
        radius: width / 2
        x: cx - width / 2
        y: cy - width / 2
        color: root.rippleColor
        opacity: 0

        ParallelAnimation {
            id: rippleAnim

            NumberAnimation {
                target: circle
                property: "width"
                from: 0
                to: circle.maxDiameter
                duration: Motion.duration_medium_2
                easing: Motion.standard
            }

            SequentialAnimation {
                PropertyAction {
                    target: circle
                    property: "opacity"
                    value: StateLayer.pressed
                }

                NumberAnimation {
                    target: circle
                    property: "opacity"
                    to: 0
                    duration: Motion.duration_medium_2
                    easing: Motion.standard
                }
            }
        }
    }

    TapHandler {
        id: tap

        enabled: root.interactive
        onPressedChanged: {
            if (pressed)
                root.start(point.position.x, point.position.y);
        }
        onTapped: root.tapped()
    }

    // Restart the ripple from a fresh press point. Stopping first resets
    // any in-flight sweep so rapid taps each get their own ripple rather
    // than stacking on a half-faded one.
    function start(px, py) {
        rippleAnim.stop();
        circle.cx = px;
        circle.cy = py;
        circle.width = 0;
        rippleAnim.start();
    }
}
