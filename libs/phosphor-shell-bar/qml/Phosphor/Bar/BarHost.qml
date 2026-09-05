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
// A popout hangs from the bar as a PANE tied to the rail by a 2 px tether
// in the rail's hue at the chip's x (A2 §4.3). Two forms:
//
//   - ENGINE-PLACED (`paneExternal`): the pane is a real toplevel the
//     placement engine positions (A2 §4.1). The bar draws only the tether,
//     routed along the rail to wherever the placement map says the pane
//     landed (`paneScreenRect`), and the pane's own surface draws the rest.
//   - FLOATING FALLBACK (A2 §4.2, last row): a surface drawn 0 px under
//     the band at the chip's x, inside the bar's own PanelWindow, for an
//     output with no placement engine.
//
// Both share `_paneProgress`, so the open/close choreography (A2 §4.4)
// is one animation either way: tether first, content after; content
// releases first, tether retracts last.
//
// The bar has one pane of its own besides the host's: the EXPANDED
// PLACEMENT MAP (A2 §1.1), opened by a long-press or right-click on the
// map chip and drawn inline under it (`mapPaneOpen`). It uses the same
// tether and choreography, anchored to "placementmap". At most one pane
// per screen (A2 §4.7): the host's pane opening closes the map pane, and
// the map pane does not open while the host's is up.
//
// Urgency (A2 §3.3, §6): while any cell on this screen's map demands
// attention, the rail over the map chip thickens 2 → 4 px in white,
// entering over 200 ms and pulsing with the cell.
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
    // True when the open pane is an engine-placed toplevel: the inline
    // pane stays down and the tether routes to `paneScreenRect`.
    property bool paneExternal: false
    // Widget id whose chip the pane hangs under and the tether drops from.
    property string paneAnchor: "controlcenter"
    property int paneWidth: 380
    property int paneDepth: 460

    // ─── The map pane (the bar's own) ───────────────────────────────────
    // Open when the map chip was long-pressed or right-clicked. Toggled
    // by the chip; closed by the pane's own close row or by the host's
    // pane opening.
    property bool mapPaneOpen: false
    // Opened on its menu section (a right-click).
    property bool mapPaneMenuFocused: false
    property int mapPaneWidth: 360

    // What the pane machinery below actually shows: the host's pane, or
    // the map pane when that is the one open.
    readonly property bool _paneOpenEff: panel.paneOpen || panel.mapPaneOpen
    readonly property bool _paneExternalEff: panel.mapPaneOpen ? false : panel.paneExternal
    readonly property string _paneAnchorEff: panel.mapPaneOpen ? "placementmap" : panel.paneAnchor
    readonly property int _paneWidthEff: panel.mapPaneOpen ? panel.mapPaneWidth : panel.paneWidth

    // The map chip, once its slot has mounted it, for the expand request
    // and the urgency thickening.
    readonly property Item _mapWidget: {
        void leftSlot.mountedCount;
        void centerSlot.mountedCount;
        void rightSlot.mountedCount;
        const c = leftSlot.cellFor("placementmap") || centerSlot.cellFor("placementmap") || rightSlot.cellFor("placementmap");
        return c && c.widget ? c.widget : null;
    }

    Connections {
        target: panel._mapWidget

        function onExpandRequested(menu: bool): void {
            // One pane per screen: the host's pane wins while it is up.
            if (panel.paneOpen)
                return;
            if (panel.mapPaneOpen && !menu) {
                panel.mapPaneOpen = false;
                return;
            }
            panel.mapPaneMenuFocused = menu;
            panel.mapPaneOpen = true;
        }
    }

    Binding {
        target: panel._mapWidget
        property: "expanded"
        value: panel.mapPaneOpen
        when: panel._mapWidget !== null && panel._mapWidget.expanded !== undefined
    }

    // The band, for a host that requests compositor effects behind it
    // (phosphor-glass is real backdrop blur under the navy tint, and only
    // the compositor can blur what is behind a surface).
    readonly property Item bandItem: band
    readonly property rect bandRect: Qt.rect(0, Tokens.rail_thickness, panel.width, Tokens.bar_thickness - Tokens.rail_thickness)

    // Where the engine put the external pane, in this screen's pixels, or
    // an empty rect while unknown. A Wayland client is never told where
    // its toplevel went, so the bar reads it off the placement map: the
    // engines focus a window they place, and the cell that BECOMES focused
    // after the pane opens is the pane's (a tile or column of its own, or
    // the zone it snapped into). Latched once per open; the rect then
    // follows the cell as the map changes (a scrolled strip, a reflow).
    // A pane that landed in the cell that was already focused never
    // changes focus, so it is not located and the tether drops straight.
    readonly property rect paneScreenRect: {
        void panel._mapEpoch;
        if (!panel._paneExternalEff || !panel._paneOpenEff || panel._paneCellId === "" || !panel.placementMap)
            return Qt.rect(0, 0, 0, 0);
        return panel.placementMap.cellRect(panel._paneCellId);
    }
    property string _paneCellId: ""
    property string _focusAtOpen: ""
    property int _mapEpoch: 0

    on_PaneOpenEffChanged: {
        panel._paneCellId = "";
        panel._focusAtOpen = panel._paneOpenEff && panel.placementMap ? panel.placementMap.focusedCellId() : "";
    }
    // A second pane replaces the first (A2 §4.7): the host's pane opening
    // closes the map pane.
    onPaneOpenChanged: {
        if (panel.paneOpen)
            panel.mapPaneOpen = false;
    }

    Connections {
        target: panel.placementMap

        function onChanged(): void {
            panel._mapEpoch++;
            if (!panel._paneOpenEff || !panel._paneExternalEff || panel._paneCellId !== "")
                return;
            const focused = panel.placementMap.focusedCellId();
            if (focused !== "" && focused !== panel._focusAtOpen)
                panel._paneCellId = focused;
        }
    }
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
    property real _paneProgress: panel._paneOpenEff ? 1 : 0

    Behavior on _paneProgress {
        NumberAnimation {
            duration: panel._paneOpenEff ? Motion.duration_reveal + Motion.duration_enter_content : Motion.duration_dismiss + 250
            easing: panel._paneOpenEff ? Motion.reveal : Motion.release
            onFinished: {
                if (!panel._paneOpenEff)
                    panel.paneClosed();
            }
        }
    }

    shadowSize: panel.paneReserve

    // Input region covers the pane only while it is open or in flight.
    // Quantised, so a NumberAnimation does not issue a Wayland input-region
    // update per frame. `thickness` is deliberately not widened: that is
    // the exclusive zone, and growing it would shove tiled windows.
    // An external pane takes its own input on its own surface; the strip
    // below the band then only carries the tether, which is not clickable.
    interactiveThickness: panel._paneProgress > 0.01 && !panel._paneExternalEff ? Tokens.bar_thickness + panel._usablePaneDepth : 0

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
        // phosphor-glass as the pack defines it for the bar (A2 §3.2):
        // the navy tint at its 0.55 depth over a real backdrop blur, glow
        // and sweep off. The blur is the compositor's, requested behind
        // `bandRect` by the host (ShellEffects), since a client cannot
        // sample what lies behind its own surface; this rectangle is the
        // tint. Without a blur backend it reads as a plain navy tint.
        color: Theme.surface
        opacity: 0.55
    }

    // ─── The rail ───────────────────────────────────────────────────────
    SpectrumRail {
        id: rail

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        gleam: true
        // Bound axis on a scrolling screen: the rail shows the same slice
        // of the strip's gradient as the lens (A2 §3.3). The lens is the
        // viewport's fraction of the strip from Scrolling.stripModelJson,
        // so it narrows as columns pile up off screen; against an older
        // daemon the visible-cut fallback gives a full-width lens.
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

    // Urgency: while any cell on the map demands attention, the rail over
    // the map chip thickens 2 → 4 px in white (A2 §3.3), entering over
    // 200 ms into the band (never into the screen) and pulsing 0.4 → 1.0
    // at 1.2 s with the cell. Steady under reduced motion. Releases when
    // clear.
    Rectangle {
        id: railUrgency

        readonly property Item _cell: {
            void leftSlot.mountedCount;
            void centerSlot.mountedCount;
            void rightSlot.mountedCount;
            return leftSlot.cellFor("placementmap") || centerSlot.cellFor("placementmap") || rightSlot.cellFor("placementmap");
        }
        readonly property bool active: panel._mapWidget !== null && panel._mapWidget.urgent === true && _cell !== null
        property real pulse: 1

        visible: height > 0 && _cell !== null
        x: _cell ? _cell.mapToItem(panel.contentItem, 0, 0).x : 0
        y: 0
        width: _cell ? _cell.width : 0
        height: active ? Tokens.rail_thickness * 2 : 0
        color: Spectrum.focus
        opacity: active ? pulse : 0

        Behavior on height {
            NumberAnimation {
                duration: railUrgency.active ? Motion.duration_short_4 : Motion.duration_release
                easing: railUrgency.active ? Motion.reveal : Motion.release
            }
        }
        Behavior on opacity {
            NumberAnimation {
                duration: Motion.duration_release
                easing: Motion.release
            }
        }
        SequentialAnimation on pulse {
            running: railUrgency.active && !Motion.reducedMotion
            loops: Animation.Infinite
            NumberAnimation {
                from: 0.4
                to: 1.0
                duration: 600
                easing: Motion.decelerated
            }
            NumberAnimation {
                from: 1.0
                to: 0.4
                duration: 600
                easing: Motion.accelerated
            }
            onRunningChanged: {
                if (!running)
                    railUrgency.pulse = 1;
            }
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
        return leftSlot.cellFor(panel._paneAnchorEff) || centerSlot.cellFor(panel._paneAnchorEff) || rightSlot.cellFor(panel._paneAnchorEff);
    }
    readonly property real _anchorCenterX: {
        void panel.width;
        const c = panel._anchorCell;
        if (!c)
            return panel.width / 2;
        return c.mapToItem(panel.contentItem, 0, 0).x + c.width / 2;
    }
    readonly property real _paneW: Math.min(panel._paneWidthEff, panel.width)
    // The map pane is as deep as its content; the host's pane takes the
    // reserved depth.
    readonly property int _paneDepthEff: panel.mapPaneOpen && mapContent.item && mapContent.item.implicitHeight > 0 ? Math.min(panel._usablePaneDepth, Math.round(mapContent.item.implicitHeight) + Tokens.rail_thickness) : panel._usablePaneDepth
    // Right-aligned under the chip, clamped to the screen: a trailing chip
    // gets a pane that ends where the chip ends.
    readonly property real _paneX: Math.max(0, Math.min(panel.width - panel._paneW, panel._anchorCenterX + Tokens.spacing_l - panel._paneW))
    readonly property real _paneT: Spectrum.tForX(panel._paneX + panel._paneW / 2, panel.width)

    // The tether: rail hue at the chip's x, dropping from the rail to the
    // pane's top edge. First to arrive, last to leave. For an external
    // pane it runs along the rail to the pane's x when the pane is not
    // under the chip, and drops to the pane's top edge (which lies in the
    // surface strip reserved below the band); for the inline pane, and
    // for an external pane not yet located, it drops to the band's edge.
    PaneTether {
        id: tether

        anchors.fill: parent
        anchorX: panel._anchorCenterX
        paneRect: panel.paneScreenRect
        screenWidth: panel.width
        progress: panel._paneProgress
        sliceStart: rail.sliceStart
        sliceEnd: rail.sliceEnd
    }

    // The pane.
    Item {
        id: pane

        x: panel._paneX
        y: Tokens.bar_thickness
        width: panel._paneW
        height: Math.max(0, panel._paneDepthEff * Math.max(0, Math.min(1, (panel._paneProgress - 0.2) / 0.8)))
        clip: true
        // Never for an external pane: the toplevel is the pane then.
        visible: height > 0 && !panel._paneExternalEff
        // Gate input the instant a close starts: an opacity-0 Item is still
        // hit-testable, and the collapse outlasts the fade.
        enabled: panel._paneOpenEff && !panel._paneExternalEff

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
            active: panel._paneOpenEff
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
                if (panel.paneOpen && !panel.paneExternal)
                    pane.everOpened = true;
            }
        }

        // Content enters after the tether and the surface: opacity plus
        // a 4 px slide, no scale.
        readonly property real _p: Math.max(0, Math.min(1, (panel._paneProgress - 0.5) / 0.5))

        Loader {
            id: content

            anchors.fill: parent
            anchors.topMargin: Tokens.rail_thickness
            opacity: pane._p
            anchors.bottomMargin: -4 * (1 - pane._p)
            // Built on first open and kept: the tiles hold live service
            // connections that would be re-enumerated on every open. Hidden,
            // not unloaded, while the map pane has the surface.
            active: (panel.paneOpen && !panel.paneExternal) || pane.everOpened
            visible: !mapContent.showing
            sourceComponent: panel.paneContent
        }

        // The map pane's content, in its own loader so opening it never
        // tears down the host's kept content. Kept through its close so
        // the content releases before the surface collapses (A2 §4.4).
        Loader {
            id: mapContent

            property bool showing: false

            Connections {
                target: panel

                function onMapPaneOpenChanged(): void {
                    if (panel.mapPaneOpen)
                        mapContent.showing = true;
                }
                function on_PaneProgressChanged(): void {
                    if (!panel.mapPaneOpen && panel._paneProgress <= 0.001)
                        mapContent.showing = false;
                }
            }

            anchors.fill: parent
            anchors.topMargin: Tokens.rail_thickness
            opacity: pane._p
            anchors.bottomMargin: -4 * (1 - pane._p)
            active: showing
            visible: showing
            sourceComponent: MapPane {
                map: panel.placementMap
                menuFocused: panel.mapPaneMenuFocused
                onCloseRequested: panel.mapPaneOpen = false
            }
        }
    }
}
