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
    // The surface pack on the pane (A1 §2.4, `shell.phosphor.popout`),
    // set by the transport from the composition root. With a chain
    // engaged the pane's own stroke steps aside for the pack's.
    property Component decoration: null
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
        id: ground

        // The pack's capture item: the ground alone, so the content stays
        // crisp and interactive.
        property bool shaderAnchor: true

        anchors.fill: parent
        radius: Tokens.radius_tile
        color: Theme.surface_container
        // Fades with the rest of the pane, and NOT painted flat from the
        // instant the toplevel maps.
        //
        // A pane is a real window, so the compositor maps it at ITS chosen
        // position and the engine's rule moves it afterwards. Measured in
        // the nested harness on a 1024x768 output: mapped at 322,168
        // 380x460 — dead centre — and moved to 516,36 500x284 fifty-six
        // milliseconds later. At a flat 0.96 that is three or four frames
        // of a solid, fully opaque card sitting in the middle of the
        // screen before it jumps into its zone, which is exactly the
        // "starts in the centre and pops into place" the panes were doing.
        //
        // Riding _progress costs nothing — the enter animation already
        // runs — and leaves the pane at a fraction of its opacity while it
        // is still mis-placed rather than solid.
        //
        // It does NOT close the window: the jump is the compositor's, and a
        // Wayland client cannot see its own toplevel position. The complete
        // fix is placing the pane on its first configure, in the daemon's
        // rule path.
        //
        // An earlier attempt also HELD the enter for the 140 ms placement
        // window of A2 §4.4. That closed the window but delayed every pane
        // open by 140 ms to hide a flash nobody had actually observed:
        // screenshot latency is longer than the window it was meant to
        // catch, so it was never captured, only inferred from the geometry.
        // Not worth the latency. Do not re-add it without a capture of the
        // flash first.
        opacity: 0.96 * root._progress
    }

    DecorationSlot {
        id: decorationSlot

        anchors.fill: parent
        component: root.decoration
        contentItem: ground
        surfacePath: "shell.phosphor.popout"
        focused: root.open
    }

    SpectrumStroke {
        anchors.fill: parent
        radius: Tokens.radius_tile
        t: root.hueT
        active: root.open
        visible: !decorationSlot.active
        // Fades with the ground, for the same reason: until the placement
        // window has elapsed this pane may still be sitting wherever the
        // compositor mapped it, and an outline drawn there is an outline
        // drawn in the wrong place.
        opacity: root._progress
    }

    // The top-edge band, the rail's gradient over this pane's x-range.
    // Gated like everything else on this surface: it is the brightest
    // thing the pane draws, so a band painted at the map position is the
    // most visible part of the centred flash.
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Tokens.rail_thickness
        opacity: root._progress
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
        // Arm the placement window on the way in; drop it on the way out so
        // the next open waits again rather than entering instantly.
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
