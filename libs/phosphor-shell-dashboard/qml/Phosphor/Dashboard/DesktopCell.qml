// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.DesktopCell, one desktop's placement map in the
// dashboard grid (A3 §7 b).
//
// A PlacementMiniature with labels at the cell size, under a 1 px
// outline: blue when this is the current desktop, cyan otherwise, rose
// while the desktop holds an urgent window, white while hovered (A3 §7
// e). Non-current cells sit at 62 % (the focus-fade of the mock). The
// 12 px label top-left reads `N · Mode`. Click goes there.

import QtQuick
import Phosphor.Theme
import Phosphor.Widgets

Item {
    id: root

    required property int index
    // The desktop's map: PlacementMap.forScreenDesktop(screen, index).
    property var map: null
    property bool current: false
    property string name: ""

    signal activated

    readonly property bool hovered: hover.hovered
    readonly property bool urgent: map ? map.urgent : false
    readonly property int mode: map ? map.mode : -1
    readonly property string modeName: {
        switch (mode) {
        case 0:
            return qsTr("Snapping");
        case 1:
            return qsTr("Tiling");
        case 2:
            return qsTr("Scrolling");
        default:
            return qsTr("Empty");
        }
    }
    readonly property color _edge: hovered ? Spectrum.focus : urgent ? Spectrum.hot : current ? Spectrum.active : Spectrum.resting

    opacity: current || hovered ? 1 : 0.62
    Behavior on opacity {
        NumberAnimation {
            duration: Motion.duration_enter_content
            easing: Motion.reveal
        }
    }

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Desktop %1, %2").arg(index + 1).arg(modeName)

    Rectangle {
        anchors.fill: parent
        radius: Tokens.radius_edge
        color: "transparent"
        border.width: 1
        border.color: root._edge

        Behavior on border.color {
            ColorAnimation {
                duration: Motion.duration_enter
                easing: Motion.enter
            }
        }
    }

    // `N · Mode`, 12 px, top-left.
    TabularText {
        id: label

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: Tokens.spacing_s
        text: (root.index + 1) + " · " + root.modeName
        font.pixelSize: Tokens.font_size_label_m
        color: root.current ? Theme.on_surface : Theme.on_surface_variant
    }

    PlacementMiniature {
        id: mini

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: label.bottom
        anchors.bottom: parent.bottom
        anchors.margins: Tokens.spacing_s
        model: root.map
        labels: true
        cellRadius: Tokens.radius_mini
        // Not interactive: a click anywhere in the cell goes to the
        // desktop (A3 §7 d); the per-window verbs are the bar map's.
        interactive: false
    }

    // An empty desktop says so, in the mock's words.
    Text {
        anchors.centerIn: mini
        visible: root.map && root.map.cells.length === 0
        text: qsTr("no windows")
        color: Theme.on_surface_variant
        font.family: Tokens.font_family_ui
        font.pixelSize: Tokens.font_size_label_m
        opacity: 0.7
    }

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onTapped: root.activated()
    }
}
