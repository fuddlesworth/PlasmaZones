// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.BarHost, the spectrum-rail bar surface.
//
// One layer-shell PanelWindow per output painting the bar of
// docs/phosphor-shell-design/05-visual-identity.md: a 2 px spectrum rail
// on the screen edge (the screen's x-axis painted cyan → rose) over a
// 26 px band of navy, 0 inset, no radius, no shadow. Widgets are bare
// content on the band, arranged in three slots (left / center / right)
// mounted through the shell's IBarWidgetFactory registry (the
// `BarRegistry` context property), so the bar owns no widget code.
//
// A popout hangs from the bar as a PANE: a surface drawn 0 px under the
// band at the x of the chip that owns it, tied to the rail by a 2 px
// tether in the rail's hue at that x (A2 §4.3). Phase 1 draws the pane
// inside the bar's own PanelWindow (the floating fallback of A2 §4.2);
// the engine-placed pane is phase 2 and needs a real toplevel.
//
//   BarHost { }   // defaults match mockups-v2/bar-widgets.svg
//
// The centre slot anchors to the bar's true centre while the side slots
// anchor to the edges, so the clock stays optically centred.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets
import Phosphor.Shell

PanelWindow {
    id: panel

    edge: PanelWindow.Top
    panelLayer: PanelWindow.LayerTop
    // The exclusive zone is the whole bar: rail plus band. The engines'
    // own outer gap separates windows from the band.
    thickness: Tokens.bar_thickness
    alignment: PanelWindow.Fill
    // The bar never wants keyboard focus (Plasma-panel behaviour); a pane
    // painted inside it inherits this, so keyboard handling in pane
    // content is inert on a real screen until the pane is its own
    // surface (phase 2). Pointer interaction is unaffected.
    keyboardFocus: PanelWindow.None

    // ─── Pane (the bar-anchored popout) ─────────────────────────────────
    //
    // Content, mounted when `paneOpen` first goes true and kept.
    property Component paneContent: null
    property bool paneOpen: false
    // Widget id whose chip the pane hangs under and the tether drops from.
    property string paneAnchor: "controlcenter"
    property int paneWidth: 380
    property int paneDepth: 460
    // What the pane can take on THIS output: the requested depth or what is
    // left below the bar, whichever is less, so a short display never gets
    // a surface the compositor clips.
    readonly property int _usablePaneDepth: {
        const available = (panel.screen ? panel.screen.height : 0) - Tokens.bar_thickness - Tokens.spacing_xl;
        return available > 0 ? Math.min(panel.paneDepth, available) : panel.paneDepth;
    }
    // Surface reserved below the band for the pane. Reserved ONCE at
    // materialization: ShellEngine snapshots `thickness + shadowSize` when
    // it creates the layer surface and never resizes it. Costs nothing
    // while closed: the strip is transparent and outside the input region.
    property int paneReserve: panel._usablePaneDepth

    // Emitted when the pane has finished closing; nothing is on screen.
    signal paneClosed

    // Open/close progress, 0..1. Everything in the pane derives from it so
    // one animation carries the whole choreography (A2 §4.4): tether drops
    // first, content enters after, and on close the content leaves first
    // and the tether retracts.
    property real _paneProgress: panel.paneOpen ? 1 : 0

    Behavior on _paneProgress {
        NumberAnimation {
            duration: panel.paneOpen ? Motion.duration_reveal + Motion.duration_enter_content : Motion.duration_dismiss + 250
            easing: panel.paneOpen ? Motion.reveal : Motion.release
            onFinished: {
                if (!panel.paneOpen)
                    panel.paneClosed();
            }
        }
    }

    shadowSize: panel.paneReserve

    // Input region covers the pane only while it is open or in flight.
    // Quantised, so a NumberAnimation does not issue a Wayland input-region
    // update per frame. `thickness` is deliberately not widened: that is
    // the exclusive zone, and growing it would shove tiled windows.
    interactiveThickness: panel._paneProgress > 0.01 ? Tokens.bar_thickness + panel._usablePaneDepth : 0

    // Bar layout: each slot is a list of groups; each group is an array of
    // widget ids separated from its neighbours by a hairline.
    property var leftGroups: [["placementmap"], ["focusedapp"]]
    property var centerGroups: [["clock"]]
    property var rightGroups: [["systemmetrics"], ["media"], ["tray"], ["audio", "network", "bluetooth", "battery"], ["notification", "controlcenter", "power"]]

    // This screen's placement map, shared by the rail (which binds its
    // slice to the strip on a scrolling screen) and the map widget.
    readonly property var placementMap: panel.screen ? PlacementMap.forScreen(panel.screen.name) : null
    readonly property bool _scrolling: panel.placementMap ? panel.placementMap.mode === 2 : false
    readonly property var _lens: panel.placementMap && panel._scrolling ? panel.placementMap.lens : null

    // ─── The band ───────────────────────────────────────────────────────
    Rectangle {
        id: band

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Tokens.rail_thickness
        height: Tokens.bar_thickness - Tokens.rail_thickness
        // Phase 1 has no backdrop blur; phosphor-glass (navy tint over the
        // windows behind) is the phase-2 material for this band.
        color: Theme.surface
        opacity: 0.72
    }

    // ─── The rail ───────────────────────────────────────────────────────
    SpectrumRail {
        id: rail

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        gleam: true
        // Bound axis on a scrolling screen: the rail shows the same slice
        // of the strip's gradient as the lens (A2 §3.3). The visible-cut
        // fallback gives a full-width lens, so this only narrows once the
        // daemon exposes the strip extent.
        sliceStart: panel._lens && panel._lens.x !== undefined ? panel._lens.x : 0
        sliceEnd: panel._lens && panel._lens.w !== undefined ? panel._lens.x + panel._lens.w : 1
    }

    // Overflow ends: on a scrolling screen with columns beyond an edge the
    // rail brightens and thickens over the last 48 px on that side.
    Repeater {
        model: 2
        delegate: Rectangle {
            required property int index
            readonly property bool _left: index === 0
            readonly property int _count: panel.placementMap ? (_left ? panel.placementMap.overflowLeft : panel.placementMap.overflowRight) : 0
            visible: panel._scrolling && _count > 0
            x: _left ? 0 : panel.width - width
            y: 0
            width: 48
            height: Tokens.rail_thickness + 1
            color: Spectrum.at(_left ? Math.max(0, rail.sliceStart - 0.05) : Math.min(1, rail.sliceEnd + 0.05))
        }
    }

    // Hover highlight: the rail segment above a hovered chip at full
    // opacity, the same gradient sampled at the same x.
    Item {
        id: railHighlight

        readonly property Item _cell: leftSlot.hoveredCell || centerSlot.hoveredCell || rightSlot.hoveredCell || _anchorCell
        readonly property Item _anchorCell: panel._paneProgress > 0.01 ? panel._anchorCell : null
        readonly property real _x: _cell ? _cell.mapToItem(panel.contentItem, 0, 0).x : 0
        readonly property real _w: _cell ? _cell.width : 0

        visible: _cell !== null
        x: _x
        width: _w
        height: Tokens.rail_thickness
        clip: true

        SpectrumRail {
            x: -railHighlight._x
            width: panel.width
            height: Tokens.rail_thickness
            opacity: 1
            sliceStart: rail.sliceStart
            sliceEnd: rail.sliceEnd
        }
    }

    // ─── Slots ──────────────────────────────────────────────────────────
    Slot {
        id: leftSlot

        anchors.left: parent.left
        anchors.leftMargin: Tokens.spacing_m
        anchors.verticalCenter: band.verticalCenter
        groups: panel.leftGroups
        registry: BarRegistry
        screenWidth: panel.width
        screenName: panel.screen ? panel.screen.name : ""
    }

    Slot {
        id: centerSlot

        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: band.verticalCenter
        groups: panel.centerGroups
        registry: BarRegistry
        screenWidth: panel.width
        screenName: panel.screen ? panel.screen.name : ""
    }

    Slot {
        id: rightSlot

        anchors.right: parent.right
        anchors.rightMargin: Tokens.spacing_m
        anchors.verticalCenter: band.verticalCenter
        groups: panel.rightGroups
        registry: BarRegistry
        screenWidth: panel.width
        screenName: panel.screen ? panel.screen.name : ""
    }

    // ─── Pane geometry ──────────────────────────────────────────────────
    // The anchor chip, looked up across the slots. Re-evaluated as cells
    // mount (each slot bumps `mountedCount`).
    readonly property Item _anchorCell: {
        void leftSlot.mountedCount;
        void centerSlot.mountedCount;
        void rightSlot.mountedCount;
        return leftSlot.cellFor(panel.paneAnchor) || centerSlot.cellFor(panel.paneAnchor) || rightSlot.cellFor(panel.paneAnchor);
    }
    readonly property real _anchorCenterX: {
        void panel.width;
        const c = panel._anchorCell;
        if (!c)
            return panel.width / 2;
        return c.mapToItem(panel.contentItem, 0, 0).x + c.width / 2;
    }
    readonly property real _paneW: Math.min(panel.paneWidth, panel.width)
    // Right-aligned under the chip, clamped to the screen: a trailing chip
    // gets a pane that ends where the chip ends.
    readonly property real _paneX: Math.max(0, Math.min(panel.width - panel._paneW, panel._anchorCenterX + Tokens.spacing_l - panel._paneW))
    readonly property real _paneT: Spectrum.tForX(panel._paneX + panel._paneW / 2, panel.width)

    // The tether: rail hue at the chip's x, dropping from the rail to the
    // pane's top edge. First to arrive, last to leave.
    Rectangle {
        id: tether

        x: panel._anchorCenterX - width / 2
        y: Tokens.rail_thickness
        width: Tokens.rail_thickness
        height: Math.max(0, Math.min(1, panel._paneProgress * 1.6)) * (Tokens.bar_thickness - Tokens.rail_thickness)
        color: Spectrum.at(Spectrum.tForX(panel._anchorCenterX, panel.width))
        visible: height > 0
    }

    // The pane.
    Item {
        id: pane

        x: panel._paneX
        y: Tokens.bar_thickness
        width: panel._paneW
        height: Math.max(0, panel._usablePaneDepth * Math.max(0, Math.min(1, (panel._paneProgress - 0.2) / 0.8)))
        clip: true
        visible: height > 0
        // Gate input the instant a close starts: an opacity-0 Item is still
        // hit-testable, and the collapse outlasts the fade.
        enabled: panel.paneOpen

        Rectangle {
            anchors.fill: parent
            radius: Tokens.radius_tile
            color: Theme.surface_container
            opacity: 0.96
        }
        SpectrumStroke {
            anchors.fill: parent
            radius: Tokens.radius_tile
            t: panel._paneT
            active: panel.paneOpen
        }
        // The pane's top edge carries the rail gradient over its own
        // x-range, so it matches the bar above it in hue.
        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Tokens.rail_thickness
            clip: true
            SpectrumRail {
                x: -pane.x
                width: panel.width
                height: Tokens.rail_thickness
                opacity: 1
            }
        }

        // Latched by a plain flag rather than the Loader reading its own
        // `item` inside its own `active` binding (a cycle).
        property bool everOpened: false

        Connections {
            target: panel

            function onPaneOpenChanged() {
                if (panel.paneOpen)
                    pane.everOpened = true;
            }
        }

        Loader {
            id: content

            anchors.fill: parent
            anchors.topMargin: Tokens.rail_thickness
            // Content enters after the tether and the surface: opacity plus
            // a 4 px slide, no scale.
            readonly property real _p: Math.max(0, Math.min(1, (panel._paneProgress - 0.5) / 0.5))
            opacity: _p
            anchors.bottomMargin: -4 * (1 - _p)
            // Built on first open and kept: the tiles hold live service
            // connections that would be re-enumerated on every open.
            active: panel.paneOpen || pane.everOpened
            sourceComponent: panel.paneContent
        }
    }
}
