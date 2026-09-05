// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PlacementMap, the live map of the placement engine.
//
// Replaces the workspace pager. An 18 px, aspect-true miniature of this
// screen's placement geometry (zones, tile rects or the scrolling strip)
// drawn from the engine's own model through Phosphor.Shell.PlacementMap,
// with one 2 × 1 px tick per virtual desktop under it (A2 §1). Hover
// grows it to 22 px; click activates a cell; wheel pans the strip or
// steps focus; Ctrl+wheel switches desktop.
//
// The map draws whatever mode the screen is in right now; the shape IS
// the mode, so there is no label.

import QtQuick
import Phosphor.Shell as Shell
import Phosphor.Theme
import Phosphor.Widgets

BarWidget {
    id: root

    // The bar's screen name, bound by the slot that mounts this widget.
    property string screenName: ""
    readonly property var map: root.screenName.length > 0 ? Shell.PlacementMap.forScreen(root.screenName) : null

    readonly property int restHeight: 18
    readonly property int hoverHeight: 22
    readonly property real _aspect: root.map && root.map.aspect > 0 ? root.map.aspect : 16 / 9
    readonly property int _mapH: hover.hovered ? root.hoverHeight : root.restHeight
    readonly property int _mapW: Math.round(Math.max(24, Math.min(56, root._mapH * root._aspect)))

    available: root.map !== null
    contentWidth: root._mapW
    contentHeight: root.restHeight + 4

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Placement map")

    HoverHandler {
        id: hover

        cursorShape: Qt.PointingHandCursor
    }

    PlacementMiniature {
        id: mini

        anchors.horizontalCenter: parent.horizontalCenter
        // Grows out of the band, never into the exclusive zone.
        anchors.top: parent.top
        anchors.topMargin: (root.restHeight - root._mapH) / 2
        width: root._mapW
        height: root._mapH
        model: root.map
        interactive: true
        onCellClicked: id => {
            if (root.map)
                root.map.activate(id);
        }

        Behavior on height {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter_content : Motion.duration_release
                easing: hover.hovered ? Motion.reveal : Motion.release
            }
        }
    }

    // Desktop ticks: the only trace of the old dots, demoted to a ruler.
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: root.restHeight + 1
        spacing: 2

        Repeater {
            model: root.map ? root.map.desktopCount : 0
            delegate: Rectangle {
                required property int index
                width: 2
                height: 1
                color: Spectrum.focus
                opacity: root.map && index === root.map.currentDesktop ? 0.9 : 0.25
            }
        }
    }

    WheelHandler {
        acceptedModifiers: Qt.NoModifier
        onWheel: event => {
            if (root.map)
                root.map.scrollView(event.angleDelta.y > 0 ? -1 : 1);
        }
    }
    WheelHandler {
        acceptedModifiers: Qt.ControlModifier
        onWheel: event => {
            if (!root.map)
                return;
            const next = root.map.currentDesktop + (event.angleDelta.y > 0 ? -1 : 1);
            if (next >= 0 && next < root.map.desktopCount)
                root.map.switchDesktop(next);
        }
    }
}
