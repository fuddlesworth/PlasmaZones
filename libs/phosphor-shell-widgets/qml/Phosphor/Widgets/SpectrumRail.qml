// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.SpectrumRail, the brand ramp drawn along an edge.
//
// A thin strip that shades cyan → rose along its length: the bar's
// screen-edge rail, a pane's top band, a tether. Horizontal by default;
// `vertical` turns it for a side bar. `sliceStart` / `sliceEnd` show only
// a window of the ramp, so a scrolling screen's rail can carry the same
// slice of the strip its lens shows (05 §4.1, structure axis). `gleam`
// sends a short bright segment along the rail once every 9 s while idle,
// the packs' travelling highlight on chrome; it stays off under reduced
// motion.
//
//   SpectrumRail { anchors { left: parent.left; right: parent.right; top: parent.top } gleam: true }

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property bool vertical: false
    property int thickness: Tokens.rail_thickness
    // Window of the ramp on display, as 0..1 fractions of the full
    // cyan → rose axis. The default shows all of it.
    property real sliceStart: 0
    property real sliceEnd: 1
    property bool gleam: false

    opacity: 0.75

    // LOAD-BEARING. The gleam below is deliberately sized and travelled to
    // start fully off one end of the rail and finish fully off the other,
    // so for part of every pass it sits OUTSIDE this item's bounds. On the
    // bar that is invisible, because the rail spans the whole output and
    // the overshoot lands off-screen. Anywhere the rail is inset — a
    // panel's top band, a pane's, a tether's — the overshoot escapes and
    // paints an 8 px bright dash detached from the surface, hanging on the
    // desktop beside it.
    //
    // Clipping here rather than in each consumer: PaneHost already wraps
    // its rail in an `Item { clip: true }` for exactly this, and the bar
    // panels then hit it a second time. A shared atom whose correct use
    // requires a wrapper is a trap; it clips itself now, and the wrapper
    // in PaneHost is left in place because it is also what gives that band
    // its x-range offset.
    clip: true

    implicitWidth: vertical ? thickness : 200
    implicitHeight: vertical ? 200 : thickness

    // Sanitised slice: clamped to 0..1 and never zero-length, so the
    // position math below cannot divide by zero on a degenerate binding.
    readonly property real _s0: Math.max(0, Math.min(1, Math.min(sliceStart, sliceEnd)))
    readonly property real _s1: Math.max(_s0 + 0.0001, Math.min(1, Math.max(sliceStart, sliceEnd)))
    readonly property real _length: vertical ? height : width

    // Where base stop k (at k/3 on the full axis) lands inside the slice.
    // Stops outside the slice pin to the nearest end at the end's colour,
    // which leaves the gradient with coincident equal-colour stops there,
    // a no-op for the rasteriser.
    function _pos(k) {
        return Math.max(0, Math.min(1, (k / 3 - _s0) / (_s1 - _s0)));
    }
    function _col(k) {
        return Spectrum.at(Math.max(_s0, Math.min(_s1, k / 3)));
    }

    Rectangle {
        id: band

        anchors.fill: parent
        gradient: Gradient {
            orientation: root.vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop {
                position: 0
                color: Spectrum.at(root._s0)
            }
            GradientStop {
                position: root._pos(0)
                color: root._col(0)
            }
            GradientStop {
                position: root._pos(1)
                color: root._col(1)
            }
            GradientStop {
                position: root._pos(2)
                color: root._col(2)
            }
            GradientStop {
                position: root._pos(3)
                color: root._col(3)
            }
            GradientStop {
                position: 1
                color: Spectrum.at(root._s1)
            }
        }
    }

    // The travelling highlight: about 12 % of the rail, soft-edged, at an
    // alpha peak of 0.6 (05 §5). On a light field a highlight on white is
    // invisible, so the gleam becomes a darker segment of the same rail
    // instead (A1 §2.5).
    Rectangle {
        id: gleamSegment

        readonly property real _len: Math.max(8, root._length * 0.12)
        // 0..1 travel along the rail, animated below. Starts fully off one
        // end and finishes fully off the other.
        property real travel: 0
        readonly property color _ink: Theme.isDark ? "#FFFFFF" : "#000000"

        visible: root.gleam && !Motion.reducedMotion
        width: root.vertical ? root.thickness : _len
        height: root.vertical ? _len : root.thickness
        x: root.vertical ? 0 : travel * (root._length + _len) - _len
        y: root.vertical ? travel * (root._length + _len) - _len : 0
        gradient: Gradient {
            orientation: root.vertical ? Gradient.Vertical : Gradient.Horizontal
            GradientStop {
                position: 0
                color: Qt.rgba(gleamSegment._ink.r, gleamSegment._ink.g, gleamSegment._ink.b, 0)
            }
            GradientStop {
                position: 0.5
                color: Qt.rgba(gleamSegment._ink.r, gleamSegment._ink.g, gleamSegment._ink.b, 0.6)
            }
            GradientStop {
                position: 1
                color: Qt.rgba(gleamSegment._ink.r, gleamSegment._ink.g, gleamSegment._ink.b, 0)
            }
        }

        SequentialAnimation on travel {
            running: gleamSegment.visible
            loops: Animation.Infinite
            // One pass, then rest until the 9 s period comes round again.
            NumberAnimation {
                from: 0
                to: 1
                duration: 1400
                easing: Motion.release
            }
            PauseAnimation {
                duration: 9000 - 1400
            }
        }
    }
}
