// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BarHost, the connected-corner bar surface.
//
// A single layer-shell PanelWindow painting the signature floating
// capsule (BarCanvas atom). Widgets are arranged in three slots (left /
// center / right); each slot mounts an ordered list of widget ids through
// the shell's IBarWidgetFactory registry (the `BarRegistry` context
// property), so the catalog is pluggable and the bar owns no widget code.
//
// The capsule is inset from the screen edges (a floating island, per
// docs/phosphor-shell-design/mockups/bar-top.svg); the rest of the panel
// surface is left unpainted so the desktop shows around it. The panel
// reserves an exclusive zone of capsule-height + top-inset so windows
// tile below the bar.
//
// One bar per output. BarHost itself is a single PanelWindow and takes no
// position on how many exist: compose it under Phosphor.Shell's
// PerScreenPanels to get one per screen (what examples/phosphor-shell does),
// or instantiate it bare for a single bar on the primary output, which is
// what ShellEngine resolves an unset PanelWindow.screen to.
//
//   BarHost { }   // defaults match the mockup; override the *Groups
//                 // lists or barThickness/screenInset to customise.
//
// The centre slot is anchored to the capsule's true centre (the clock sits
// mid-screen, as the mockup shows) while the side slots anchor to the
// edges, so none of the three reserves space against the others. On an
// output narrow enough for the trailing cluster to reach the middle they
// would overlap. That is the same trade the panel this replaced made
// deliberately, and it keeps the clock optically centred rather than
// centred in whatever space the sides leave.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Shell

PanelWindow {
    id: panel

    edge: PanelWindow.Top
    panelLayer: PanelWindow.LayerTop
    // Reserve the capsule plus its top inset; the side insets are
    // horizontal and don't affect a top panel's exclusive zone. The bottom
    // deliberately gets no inset: the zone ends at the capsule's lower edge
    // so tiled windows start immediately below the bar rather than leaving a
    // permanent strip of wallpaper there.
    thickness: panel.barThickness + panel.screenInset
    // The transparent strip below the exclusive zone is still part of the
    // wl_surface, so it would swallow clicks along the top edge of whatever
    // tiles beneath. ShellEngine masks the surface's input region down to
    // the painted band (PanelWindow.visibleBand), which excludes it — see
    // `shadowSize` and `interactiveThickness` above.
    alignment: PanelWindow.Fill
    // The bar never wants keyboard focus (Plasma-panel behaviour); attached
    // popouts take their own grab.
    keyboardFocus: PanelWindow.None

    // Capsule strip height and the inset from the screen edges.
    property int barThickness: 44
    property int screenInset: Tokens.spacing_xl

    // ─── Bar-anchored popout (the connected-corner socket) ──────────────
    //
    // A popout that grows DOWNWARD out of the capsule as one continuous
    // painted surface, per docs/phosphor-shell-design/mockups/control-center.svg.
    // The host supplies the content; the bar owns the pocket geometry, the
    // growth animation, and the surface plumbing that makes the pocket
    // clickable.
    //
    // Content, mounted into the pocket when `socketOpen` first goes true.
    property Component socketContent: null
    // Whether the pocket is open. Animating `_socketDepth` off this is what
    // grows the capsule.
    property bool socketOpen: false
    // Pocket width, and its natural (fully-open) depth. The host sizes
    // these to whatever it is mounting.
    property int socketWidth: 380
    property int socketDepth: 420
    // What the pocket can actually take on THIS output, which is the
    // requested depth or whatever is left below the bar, whichever is less.
    // A short display (a 1366x768 panel, or any output at a fractional scale
    // that shrinks the logical size) would otherwise get a surface deeper
    // than the screen, and the compositor simply clips the bottom off the
    // pocket. Everything that positions or reserves for the pocket derives
    // from this rather than from the raw request.
    readonly property int _usableSocketDepth: {
        const available = (panel.screen ? panel.screen.height : 0) - panel.screenInset - panel.barThickness - Tokens.spacing_xl;
        return available > 0 ? Math.min(panel.socketDepth, available) : panel.socketDepth;
    }
    // How much surface to reserve below the capsule for the open pocket.
    // Reserved ONCE, at materialization: ShellEngine snapshots
    // `thickness + shadowSize` when it creates the layer surface and never
    // resizes it, so a pocket that needs room later must have it reserved
    // now. Costs nothing while closed — the strip is transparent and, by
    // default, outside the input region.
    property int socketReserve: panel._usableSocketDepth

    // Emitted when the pocket has finished closing, so a host can tear its
    // content down (or release a grab) only once nothing is visible.
    signal socketClosed

    // Animated pocket depth. The socket descriptor and the content clip
    // both derive from this, so one animation drives the whole growth.
    property real _socketDepth: panel.socketOpen ? panel._usableSocketDepth : 0

    Behavior on _socketDepth {
        NumberAnimation {
            id: socketAnim

            duration: Motion.duration_long_2
            easing: Motion.emphasized
            onFinished: {
                if (!panel.socketOpen)
                    panel.socketClosed();
            }
        }
    }

    // Extra surface below the exclusive zone: the capsule's drop shadow
    // plus room for the open pocket.
    shadowSize: Tokens.spacing_l + panel.socketReserve

    // Widen the INPUT REGION to cover the open pocket, and only then.
    //
    // The surface is permanently tall enough for the pocket, but
    // ShellEngine masks input down to the painted band, so without this the
    // pocket would paint and swallow nothing — every control in it dead.
    // This is the one panel geometry ShellEngine samples live; see
    // PanelWindow.interactiveThickness. Deliberately NOT `thickness`, which
    // sets the exclusive zone: widening that would shove every tiled window
    // down the screen each time a popout opened.
    //
    // 0 while fully closed hands the band back to `thickness`, so the
    // shadow strip goes click-through again rather than staying live at the
    // capsule's own depth.
    interactiveThickness: panel._socketDepth > 0.5 ? panel.screenInset + panel.barThickness + Math.ceil(panel._socketDepth) : 0

    // Bar layout: each slot is a list of groups, and each group is an
    // array of widget ids sharing one island chip. Related widgets are
    // combined (the status icons, the trailing buttons); others stand
    // alone. Defaults match the bar-top mockup; a layout editor / config
    // can override these later.
    property var leftGroups: [["workspaces"], ["focusedapp"]]
    property var centerGroups: [["clock"]]
    property var rightGroups: [["systemmetrics"], ["media"], ["tray"], ["audio", "network", "bluetooth", "battery"], ["notification", "controlcenter", "power"]]

    // The floating capsule. Inset from the panel edges so the desktop
    // shows around it; the unpainted remainder of the panel surface stays
    // transparent.
    BarCanvas {
        id: canvas

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: panel.screenInset
        anchors.leftMargin: panel.screenInset
        anchors.rightMargin: panel.screenInset

        barHeight: panel.barThickness
        // Fully-rounded capsule (radius = half height), per the mockup.
        cornerRadius: panel.barThickness / 2
        // Capsule uses the navy surface so the lighter surface_variant widget
        // chips read against it (the mockup's colour relationship).
        color: Theme.surface

        // Lift the capsule off the wallpaper as a floating island. The
        // shadow is shaped by the capsule's own alpha (the layered item),
        // so it follows the rounded corners; PanelWindow.shadowSize above
        // reserves the surface room it needs below.
        layer.enabled: true
        layer.effect: ElevationShadow {
            level: 2
        }

        // The capsule has to be tall enough to paint the pocket it grows;
        // BarCanvas draws the strip in its top `barHeight` band and the
        // socket below that.
        height: panel.barThickness + Math.max(0, panel._socketDepth)

        // Pocket geometry. Centred on the capsule, matching the mockup.
        //
        // Below ~0.5 the socket reads as closed and BarCanvas degrades to a
        // flat edge, so dropping the descriptor entirely there means the
        // pocket grows out of nothing and leaves nothing behind. Same
        // threshold the bar-canvas demo uses.
        // Clamped, and the width with it: on an output narrower than the
        // pocket plus its insets this would go negative and hang the socket
        // and its content off the left edge of the capsule.
        readonly property real socketW: Math.min(panel.socketWidth, width)
        readonly property real socketX: Math.max(0, (width - socketW) / 2)
        sockets: panel._socketDepth > 0.5 ? [
            {
                "x": canvas.socketX,
                "width": canvas.socketW,
                "depth": panel._socketDepth
            }
        ] : []

        // Pocket content, drawn over the painted socket so the two read as
        // one surface. Clipped to the pocket so the content is revealed by
        // the growth rather than sliding around inside it.
        Item {
            id: pocket

            x: canvas.socketX
            y: panel.barThickness
            width: panel.socketWidth
            height: Math.max(0, panel._socketDepth)
            clip: true
            opacity: panel.socketOpen ? 1 : 0
            // Gate input off the instant a close starts: an opacity-0 Item
            // is still hit-testable, and the depth collapse outlasts the
            // opacity fade, so without this the invisible controls stay
            // clickable through the whole close. Same trap the bar-canvas
            // demo documents.
            enabled: panel.socketOpen

            Behavior on opacity {
                NumberAnimation {
                    duration: Motion.duration_short_3
                    easing: Motion.standard
                }
            }

            Loader {
                anchors.fill: parent
                // Built on first open and kept thereafter: the tiles behind
                // it hold live service connections, and rebuilding them on
                // every open would re-enumerate NetworkManager, BlueZ and
                // PipeWire each time the user glanced at the panel.
                active: panel.socketOpen || item !== null
                sourceComponent: panel.socketContent
            }
        }

        // Default children land in the bar strip (the top barHeight band of
        // BarCanvas), so `parent` below is that strip and verticalCenter
        // centres within the capsule.
        Slot {
            id: leftSlot

            anchors.left: parent.left
            anchors.leftMargin: Tokens.spacing_l
            anchors.verticalCenter: parent.verticalCenter

            groups: panel.leftGroups
            registry: BarRegistry
        }

        Slot {
            id: centerSlot

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter

            groups: panel.centerGroups
            registry: BarRegistry
        }

        Slot {
            id: rightSlot

            anchors.right: parent.right
            anchors.rightMargin: Tokens.spacing_l
            anchors.verticalCenter: parent.verticalCenter

            groups: panel.rightGroups
            registry: BarRegistry
        }
    }
}
