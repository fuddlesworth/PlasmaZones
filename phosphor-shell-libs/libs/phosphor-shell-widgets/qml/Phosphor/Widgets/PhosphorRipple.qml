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
// Both the resting state layer and the expanding press ripple honour
// `radius`: the state layer is a rounded Rectangle, and the ripple is a
// radial gradient painted inside a rounded-rect Shape. Neither relies on
// Item.clip, which is a rectangular scissor and used to let the ripple
// bleed past the corners of a pill.

import QtQuick
import QtQuick.Shapes
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
    // Keyboard focus: a host that is activeFocus sets this so the resting
    // state layer shows the M3 focus tint (the visible focus indicator for
    // a Tab-focused control).
    property bool focused: false
    // Corner radius for the resting state-layer overlay. Match the host
    // container's radius so the hover / press tint is rounded.
    property real radius: 0

    // Live interaction state for hosts that tint other chrome on hover
    // or press (e.g. an elevation bump on a pressed button).
    readonly property bool hovered: hover.hovered
    readonly property bool down: tap.pressed
    // True while a press ripple is sweeping. Outlives `down` by the
    // fade-out, so a host that gates chrome on the animation reads this
    // rather than the press state.
    readonly property bool rippling: rippleAnim.running

    // Re-emitted tap. Hosts connect this to their own clicked signal so
    // any future ripple-side logic (focus restore, haptics) routes
    // through one place.
    signal tapped

    // Neither the state layer nor the ripple needs this any more (both
    // paint inside the rounded rect themselves), but it still bounds any
    // content a host parents into the ripple layer.
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
            if (root.focused)
                return StateLayer.focus;
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
    //
    // The sweep is painted as a radial gradient inside a rounded-rect
    // Shape rather than as a growing circle behind Item.clip, because
    // Item.clip is a rectangular scissor and would let the circle bleed
    // past `radius` at the corners. Here the painted area IS the host's
    // rounded rect, so the corners are exact by construction: the
    // gradient's opaque disc is the ripple, and everything past its edge
    // is transparent.
    // NOTE for anyone binding a property of this Shape: the ripple
    // animation below drives `opacity` through PropertyAction and
    // NumberAnimation, which are imperative writes. The first ripple
    // severs any binding on that property permanently, and the symptom
    // is a value that simply stops updating. Bind a wrapper instead, or
    // move the animation onto a helper property this one reads.
    Shape {
        id: sweep

        // Press-point centre, set by start() before each animation.
        property real cx: 0
        property real cy: 0
        // Animated ripple radius, 0 up to `maxRadius`.
        property real progress: 0
        // Radius that guarantees coverage from any press point: the full
        // diagonal reaches the far corner even from the opposite one.
        readonly property real maxRadius: Math.sqrt(root.width * root.width + root.height * root.height)

        anchors.fill: parent
        opacity: 0
        // Nothing to paint at rest, and this keeps the Shape out of the
        // scene graph between presses.
        visible: rippleAnim.running

        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"

            fillGradient: RadialGradient {
                centerX: sweep.cx
                centerY: sweep.cy
                // A zero-radius gradient is degenerate; the floor keeps
                // the first animation frame well-defined.
                centerRadius: Math.max(sweep.progress, 0.01)
                focalX: sweep.cx
                focalY: sweep.cy
                focalRadius: 0

                GradientStop {
                    position: 0
                    color: root.rippleColor
                }
                // The near-1.0 stop gives the disc a one-pixel-ish soft
                // edge instead of a hard aliased rim. Beyond the last
                // stop a radial gradient pads its final colour outward,
                // so that colour must be a fully transparent version of
                // `rippleColor` (not "transparent", which pads black at
                // zero alpha and fringes the edge dark).
                GradientStop {
                    position: 0.98
                    color: root.rippleColor
                }
                GradientStop {
                    position: 1
                    color: Qt.rgba(root.rippleColor.r, root.rippleColor.g, root.rippleColor.b, 0)
                }
            }

            PathRectangle {
                width: root.width
                height: root.height
                radius: root.radius
            }
        }

        ParallelAnimation {
            id: rippleAnim

            NumberAnimation {
                target: sweep
                property: "progress"
                from: 0
                to: sweep.maxRadius
                duration: Motion.duration_medium_2
                easing: Motion.standard
            }

            SequentialAnimation {
                PropertyAction {
                    target: sweep
                    property: "opacity"
                    value: StateLayer.pressed
                }

                NumberAnimation {
                    target: sweep
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
    function start(px: real, py: real): void {
        rippleAnim.stop();
        sweep.cx = px;
        sweep.cy = py;
        sweep.progress = 0;
        rippleAnim.start();
    }
}
