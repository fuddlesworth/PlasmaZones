// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Popout.PaneHost, the root item of an ENGINE-PLACED pane.
//
// A pane is a real xdg toplevel the placement engine positions (A2 §4.1):
// the transport puts one of these at the root of a FloatingWindow and
// parents the popout content into it. This item paints what belongs to
// the pane's own surface and nothing more:
//
//   - the ground: navy 0.96 (05 §5, "engine-placed panes") with the tile
//     radius, and the 1 px spectrum stroke every shell surface carries;
//   - the top-edge band (A2 §4.3 step 3): a 2 px SpectrumRail sampled
//     over the pane's own x-range, so it matches the bar's rail directly
//     above it. `railOffset` / `railWidth` are the pane's screen x and the
//     screen width, fed by the transport when the bar has located the
//     pane; until then the band shows the gradient over its own width;
//   - the content choreography (A2 §4.4): opacity 0 → 1 with a 4 px
//     downward slide on open, a 120 ms release on close. The engine's own
//     animation moves the surface; nothing here animates placement.
//
// The tether between the rail and this pane is the BAR's to draw (it owns
// the surface the tether crosses); this item draws only the part inside
// the pane, which is the band.
//
// Keyboard: a toplevel takes focus as a window, so Escape works here where
// it could not on the bar-painted pane. `dismissed` asks the transport to
// close; `released` reports that the close choreography has finished and
// the window may unmap.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root

    // Fills whatever holds it: the transport parents this into the
    // toplevel's content item, and the binding follows the reparent.
    anchors.fill: parent

    property Item contentItem: null
    property bool open: false
    // The pane's screen x and the screen width, for the band's slice.
    property real railOffset: 0
    property real railWidth: 0
    // Hue axis for the stroke: the pane's centre on the screen.
    readonly property real hueT: railWidth > 0 ? Spectrum.tForX(railOffset + width / 2, railWidth) : 0.5

    // The content's enter/release, 0..1. One animation, both directions.
    property real _progress: root.open ? 1 : 0

    signal dismissed
    signal released

    function dismiss() {
        root.dismissed();
    }

    focus: true
    Keys.onEscapePressed: event => {
        root.dismiss();
        event.accepted = true;
    }

    Behavior on _progress {
        NumberAnimation {
            duration: root.open ? Motion.duration_enter_content + Motion.duration_short_2 : Motion.duration_dismiss
            easing: root.open ? Motion.reveal : Motion.release
        }
    }

    // Released when the content has fully left, whether the release
    // animated or jumped (an unexposed window advances no animation, and
    // the transport must still get its unmap cue).
    on_ProgressChanged: {
        if (!root.open && root._progress === 0)
            root.released();
    }

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_tile
        color: Theme.surface_container
        opacity: 0.96
    }

    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_tile
        t: root.hueT
        active: root.open
    }

    // The top-edge band, the rail's gradient over this pane's x-range.
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Tokens.rail_thickness
        clip: true

        SpectrumRail {
            x: root.railWidth > 0 ? -root.railOffset : 0
            width: root.railWidth > 0 ? root.railWidth : root.width
            height: Tokens.rail_thickness
            opacity: 1
        }
    }

    Item {
        id: frame

        anchors.fill: parent
        anchors.topMargin: Tokens.rail_thickness
        opacity: root._progress
        anchors.bottomMargin: -4 * (1 - root._progress)
        // Input stops the instant a close starts; the fade outlasts it.
        enabled: root.open

        onChildrenChanged: {
            for (let i = 0; i < children.length; ++i)
                children[i].anchors.fill = frame;
        }
    }

    onContentItemChanged: {
        if (contentItem)
            contentItem.parent = frame;
    }
    onOpenChanged: {
        if (open) {
            forceActiveFocus();
            return;
        }
        // Closed before the enter ever moved (A2 §4.6, close during open):
        // the value is already 0, so no change will report it.
        if (root._progress === 0)
            root.released();
    }
}
