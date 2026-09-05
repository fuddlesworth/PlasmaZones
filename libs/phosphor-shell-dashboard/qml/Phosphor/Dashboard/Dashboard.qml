// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.Dashboard, every desktop's placement map at once
// (A3 §7).
//
// A full-screen overlay on the void ground at 85 %: one cell per virtual
// desktop, each that desktop's live placement map (PlacementMap
// .forScreenDesktop through `mapFor`), the current one outlined blue and
// the others under focus-fade, then the calendar and media cells in the
// same grammar as the last row. Weather is out (no service), and the
// `+` cell exists only when `workspaces` can create a desktop
// (`Workspaces` cannot, so it is omitted).
//
// Open is the shell's one scale transition (A3 §7 c, consistency table):
// the grid enters scaled so the current desktop's cell covers the
// screen and settles to 1 over 280 ms while the ground fades in; close
// reverses. It is the desktop that scales, not a UI element.
//
// Inputs are properties, never singletons, so a test can drive the
// surface with fakes: `workspaces` (model / count / activeIndex /
// switchTo, the Workspaces contract), `mapFor(index)` (a
// PlacementMapScreen per desktop), `media` (an MprisHost or null). The
// host wires `close` to the shared state; this item only asks.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

FocusScope {
    id: root

    property string screenName: ""
    // The Workspaces singleton, or a fake with the same members.
    property var workspaces: null
    // (index) => PlacementMapScreen for the desktop at that 0-based row.
    property var mapFor: null
    property var media: null
    property bool open: false
    // Cell width as a fraction of the screen width (A3 §7 b: 1/4).
    property real cellFraction: 0.25
    property int gap: Tokens.spacing_xl

    signal closeRequested

    readonly property int desktopCount: workspaces ? workspaces.count : 0
    readonly property int currentDesktop: workspaces ? workspaces.activeIndex - 1 : -1
    readonly property bool canCreate: workspaces ? typeof workspaces.create === "function" : false
    // Fixed cells: calendar, media, and the `+` when a create verb exists.
    readonly property int fixedCount: 2 + (canCreate ? 1 : 0)
    readonly property int cellCount: desktopCount + fixedCount
    // For tests: the grid's live delegate count.
    readonly property int gridCount: cells.count

    readonly property real cellW: Math.max(160, Math.round(width * cellFraction))
    readonly property real cellH: Math.round(cellW / _aspect)
    readonly property real _aspect: height > 0 && width > 0 ? width / height : 16 / 9
    readonly property int columns: Math.max(1, Math.floor((width - 2 * gap + gap) / (cellW + gap)))
    readonly property int rows: Math.ceil(cellCount / columns)

    // Enter / release drive one shared progress so the scale and the
    // fade never disagree (interrupt rule: resume from where it is).
    property real progress: 0
    Behavior on progress {
        NumberAnimation {
            duration: Motion.reducedMotion ? Motion.duration_enter_content : 280
            easing: Motion.reveal
        }
    }
    // Released fires once the close scale has run, for a host that tears
    // the surface down afterwards (the popout transport does).
    signal released
    property bool _opened: false
    onOpenChanged: {
        progress = open ? 1 : 0;
        if (open) {
            _opened = true;
            root.forceActiveFocus();
        }
    }
    onProgressChanged: {
        if (!open && _opened && progress === 0) {
            _opened = false;
            root.released();
        }
    }
    Component.onCompleted: {
        // Built with open already true (a popout builds its content fresh
        // per open): run the enter from zero rather than landing at 1.
        if (open) {
            _opened = true;
            progress = 0;
            progress = 1;
            root.forceActiveFocus();
        }
    }
    visible: progress > 0 || open
    enabled: open

    Accessible.role: Accessible.Pane
    Accessible.name: qsTr("Dashboard")

    // Esc closes (A3 §7 d).
    focus: true
    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape) {
            event.accepted = true;
            root.closeRequested();
            return;
        }
        // 1..9 jump to desktop N.
        if (event.key >= Qt.Key_1 && event.key <= Qt.Key_9) {
            const n = event.key - Qt.Key_1;
            if (n < root.desktopCount) {
                event.accepted = true;
                root.goTo(n);
            }
        }
    }

    // Go to a desktop and close. The switch goes through the map
    // (PlacementMapScreen.switchDesktop resolves the row's id in C++);
    // any of the screen's maps can do it, so the cell's own is used.
    function goTo(index: int): void {
        if (index >= 0 && index < desktopCount && mapFor) {
            const m = mapFor(index);
            if (m)
                m.switchDesktop(index);
        }
        root.closeRequested();
    }

    // Ground: void at 85 %, fading with the open progress.
    Rectangle {
        anchors.fill: parent
        color: Theme.background
        opacity: 0.85 * root.progress
    }

    // Click outside any cell closes.
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.closeRequested()
    }

    // The grid, centred, wrapping at the screen width.
    Item {
        id: grid

        readonly property real gridW: Math.min(root.columns, root.cellCount) * root.cellW + (Math.min(root.columns, root.cellCount) - 1) * root.gap
        readonly property real gridH: root.rows * root.cellH + (root.rows - 1) * root.gap
        // The current desktop's cell, the point the desktop scales from.
        readonly property int currentSlot: Math.max(0, root.currentDesktop)
        readonly property real currentX: (currentSlot % root.columns) * (root.cellW + root.gap)
        readonly property real currentY: Math.floor(currentSlot / root.columns) * (root.cellH + root.gap)
        // Scale at which the current cell covers the screen, and the origin
        // that keeps it in place while scaling: o = s * c / (s - 1).
        readonly property real fullScale: root.cellW > 0 ? root.width / root.cellW : 1
        readonly property real scale0: fullScale > 1 ? fullScale : 1
        readonly property real originX: scale0 > 1 ? scale0 * (x + currentX) / (scale0 - 1) : 0
        readonly property real originY: scale0 > 1 ? scale0 * (y + currentY) / (scale0 - 1) : 0

        x: Math.round((root.width - gridW) / 2)
        y: Math.round((root.height - gridH) / 2)
        width: gridW
        height: gridH
        opacity: root.progress

        transform: Scale {
            origin.x: grid.originX - grid.x
            origin.y: grid.originY - grid.y
            xScale: 1 + (grid.scale0 - 1) * (1 - root.progress)
            yScale: 1 + (grid.scale0 - 1) * (1 - root.progress)
        }

        Repeater {
            id: cells

            model: root.cellCount
            delegate: Item {
                id: slot

                required property int index
                readonly property bool isDesktop: index < root.desktopCount
                readonly property int fixedIndex: index - root.desktopCount

                x: (index % root.columns) * (root.cellW + root.gap)
                y: Math.floor(index / root.columns) * (root.cellH + root.gap)
                width: root.cellW
                height: root.cellH

                // Swallow the click-outside handler inside a cell.
                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        if (slot.isDesktop)
                            root.goTo(slot.index);
                        else if (slot.fixedIndex === 2)
                            root.workspaces.create();
                    }
                }

                Loader {
                    anchors.fill: parent
                    // Read by the desktop cell as parent.slotIndex: the
                    // Component is declared at root scope, where `slot`
                    // does not resolve.
                    property int slotIndex: slot.index
                    sourceComponent: slot.isDesktop ? desktopCell : slot.fixedIndex === 0 ? calendarCell : slot.fixedIndex === 1 ? mediaCell : plusCell
                }
            }
        }
    }

    Component {
        id: desktopCell

        DesktopCell {
            index: parent.slotIndex
            map: root.mapFor ? root.mapFor(index) : null
            current: index === root.currentDesktop
            onActivated: root.goTo(index)
        }
    }

    Component {
        id: calendarCell

        CalendarCell {}
    }

    Component {
        id: mediaCell

        MediaCell {
            host: root.media
        }
    }

    Component {
        id: plusCell

        Item {
            Rectangle {
                anchors.fill: parent
                radius: Tokens.radius_edge
                color: "transparent"
                border.width: 1
                border.color: plusHover.hovered ? Spectrum.focus : Spectrum.resting
                opacity: 0.62
            }
            TabularText {
                anchors.centerIn: parent
                text: "+"
                font.pixelSize: Tokens.font_size_display_m
                color: Theme.on_surface
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Tokens.spacing_m
                text: qsTr("new desktop")
                color: Theme.on_surface_variant
                font.family: Tokens.font_family_ui
                font.pixelSize: Tokens.font_size_label_m
            }
            HoverHandler {
                id: plusHover
            }
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("New desktop")
        }
    }
}
