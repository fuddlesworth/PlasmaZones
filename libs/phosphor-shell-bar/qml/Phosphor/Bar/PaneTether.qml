// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PaneTether, the wire from a chip to its pane (A2 §4.3).
//
// A 2 px line in the rail's hue that drops from the rail to the pane's
// top edge. When the pane's top edge spans the chip's x it drops straight
// down from the chip. When the engine put the pane elsewhere, the tether
// runs from the chip ALONG THE RAIL to the nearest x inside the pane
// (lighting that rail segment to full opacity, so the wire takes on the
// rail's hue as it travels) and drops from there. The rail is the wire.
//
// Coordinates are the bar surface's, whose origin is the screen's
// top-left: `anchorX` is the chip's centre, `paneRect` the pane's frame
// on the screen (an empty rect means "not located", which draws the
// straight drop to the band's bottom edge, the phase-1 form), and
// `screenWidth` scales the hue axis.
//
// `progress` is the open/close progress the bar animates: the drop grows
// over the first 60 % of it and retracts over the last, so the tether is
// first to arrive and last to leave (A2 §4.4). The routing math is exposed
// as read-only properties so it can be tested without a screen.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    property real anchorX: 0
    property rect paneRect: Qt.rect(0, 0, 0, 0)
    property real screenWidth: 0
    property real progress: 0
    // How far inside the pane's edge the drop lands when it has to travel.
    property real inset: Tokens.spacing_l
    property real barThickness: Tokens.bar_thickness
    property real railThickness: Tokens.rail_thickness
    // The rail slice on a bound-axis (scrolling) screen, so the lit run
    // matches the rail it lies on.
    property real sliceStart: 0
    property real sliceEnd: 1

    readonly property bool located: root.paneRect.width > 0 && root.paneRect.height > 0
    // The pane spans the chip when its top edge covers the chip's x.
    readonly property bool underChip: !root.located || (root.anchorX >= root.paneRect.x && root.anchorX <= root.paneRect.x + root.paneRect.width)
    // Where the drop lands: under the chip, or the nearest x inside the
    // pane (inset from its edge) when the pane sits elsewhere.
    readonly property real dropX: {
        if (root.underChip)
            return root.anchorX;
        const halfInset = Math.min(root.inset, root.paneRect.width / 2);
        return Math.max(root.paneRect.x + halfInset, Math.min(root.paneRect.x + root.paneRect.width - halfInset, root.anchorX));
    }
    // The run along the rail, empty when the drop is under the chip.
    readonly property real runStart: Math.min(root.anchorX, root.dropX)
    readonly property real runWidth: Math.abs(root.anchorX - root.dropX)
    // The drop's extent: from below the rail to the pane's top edge, or to
    // the band's bottom edge when the pane is not located.
    readonly property real dropTop: root.railThickness
    readonly property real dropBottom: root.located ? Math.max(root.barThickness, root.paneRect.y) : root.barThickness
    readonly property real fullDropHeight: Math.max(0, root.dropBottom - root.dropTop)
    readonly property real _arrival: Math.max(0, Math.min(1, root.progress * 1.6))
    readonly property real dropHeight: root.fullDropHeight * root._arrival
    readonly property real hueT: root.screenWidth > 0 ? Spectrum.tForX(root.dropX, root.screenWidth) : 0.5

    visible: root._arrival > 0

    // The lit rail run between the chip and the drop.
    Item {
        x: root.runStart
        y: 0
        width: root.runWidth * root._arrival
        height: root.railThickness
        clip: true
        visible: width > 0

        SpectrumRail {
            x: -root.runStart
            width: root.screenWidth
            height: root.railThickness
            opacity: 1
            sliceStart: root.sliceStart
            sliceEnd: root.sliceEnd
        }
    }

    // The drop.
    Rectangle {
        x: root.dropX - width / 2
        y: root.dropTop
        width: root.railThickness
        height: root.dropHeight
        color: Spectrum.at(root.hueT)
        visible: height > 0
    }
}
