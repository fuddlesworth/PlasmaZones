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
    // Extra surface below the exclusive zone so the capsule's drop shadow
    // has room to render without being clipped at the panel's bottom edge.
    //
    // The strip is transparent but still part of the wl_surface, so it
    // would swallow clicks along the top edge of whatever tiles beneath.
    // ShellEngine masks the surface's input region down to `thickness`
    // (PanelWindow.visibleBand), which excludes it.
    shadowSize: Tokens.spacing_l
    alignment: PanelWindow.Fill
    // The bar never wants keyboard focus (Plasma-panel behaviour); attached
    // popouts take their own grab.
    keyboardFocus: PanelWindow.None

    // Capsule strip height and the inset from the screen edges.
    property int barThickness: 44
    property int screenInset: Tokens.spacing_xl

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

        // Popout sockets: empty until a bar-anchored popout opens. Bar-
        // anchored popouts (control center, notification center, power
        // menu) land in later phases and will push socket descriptors here
        // so the capsule grows downward into them. Driving them will also
        // mean growing `thickness` / `shadowSize` to match, since the
        // layer-shell surface does not stretch on its own.

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
