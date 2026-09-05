// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Bar.PlacementMap, the live map of the placement engine.
//
// Replaces the workspace pager. An 18 px, aspect-true miniature of this
// screen's placement geometry (zones, tile rects or the scrolling strip)
// drawn from the engine's own model through Phosphor.Shell.PlacementMap,
// with one 2 × 1 px tick per virtual desktop under it (A2 §1). Hover
// grows it to 22 px; click activates a cell; a cell dragged onto another
// moves or swaps; middle-click floats; wheel pans the strip or steps
// focus; Ctrl+wheel switches desktop; on a scrolling screen a drag on the
// map (not on a cell) pans the strip's view 1:1 in strip space (the lens
// drag). Every gesture routes through the model, never around it.
//
// The miniature is also the compositor's DROP PROXY (A2 §1.5): whenever
// its screen rect or the cells change, the bar registers the rect and one
// rect per cell with the daemon (coalesced to one call per turn), so a
// window dragged onto the miniature lands in the real zone. Withdrawn
// when the widget goes.
//
// Long-press, or a right-click, asks the bar for the EXPANDED map (the
// bar's pane, A2 §1.1): `expandRequested(menu)`, with `menu` true when
// the pane should open on its menu section. `expanded` mirrors the pane.
//
// The map draws whatever mode the screen is in right now; the shape IS
// the mode, so there is no label.

import QtQuick
import Phosphor.Shell as Shell
import Phosphor.Theme
import Phosphor.Widgets
import "DropProxyGeometry.js" as DropProxy

BarWidget {
    id: root

    // The bar's screen name, bound by the slot that mounts this widget.
    property string screenName: ""
    readonly property var map: root.screenName.length > 0 ? Shell.PlacementMap.forScreen(root.screenName) : null
    // True while the bar shows the expanded map pane for this widget.
    property bool expanded: false
    // Any cell on this screen demands attention: the rail thickens over
    // this chip (A2 §3.3).
    readonly property bool urgent: root.map ? root.map.urgent : false

    signal expandRequested(bool menu)

    readonly property int restHeight: 18
    readonly property int hoverHeight: 22
    readonly property real _aspect: root.map && root.map.aspect > 0 ? root.map.aspect : 16 / 9
    readonly property int _mapH: hover.hovered || root.expanded ? root.hoverHeight : root.restHeight
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

    // Long-press anywhere on the chip: the expanded pane.
    TapHandler {
        acceptedButtons: Qt.LeftButton
        onLongPressed: root.expandRequested(false)
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
        onCellMoved: (fromId, toId) => {
            if (root.map)
                root.map.moveCell(fromId, toId);
        }
        onCellFloatToggled: id => {
            if (root.map)
                root.map.toggleFloat(id);
        }
        onContextMenuRequested: root.expandRequested(true)

        Behavior on height {
            NumberAnimation {
                duration: hover.hovered ? Motion.duration_enter_content : Motion.duration_release
                easing: hover.hovered ? Motion.reveal : Motion.release
            }
        }

        // Lens drag (A2 §1.5): on a scrolling screen a press-and-drag over
        // the miniature pans the real strip 1:1 in strip space, a map
        // pixel being stripExtentPx / map width strip pixels. Deltas are
        // accumulated and flushed once per frame so a fast drag is one
        // daemon call per frame rather than one per pointer event. A
        // cell's own drag handler takes the press first, so this runs
        // on the band between and around cells.
        DragHandler {
            id: lensDrag

            property real sentX: 0
            property real pendingPx: 0

            enabled: root.map !== null && root.map.mode === 2 && root.map.stripExtentPx > 0
            target: null
            xAxis.enabled: true
            yAxis.enabled: false

            function flush() {
                const px = Math.round(pendingPx);
                if (px !== 0 && root.map)
                    root.map.scrollViewByPx(px);
                // Keep the sub-pixel remainder so a slow drag still adds up.
                pendingPx -= px;
            }

            onActiveChanged: {
                if (active) {
                    sentX = 0;
                } else {
                    flush();
                }
                pendingPx = 0;
            }
            onActiveTranslationChanged: {
                if (!active || mini.width <= 0)
                    return;
                const dx = activeTranslation.x - sentX;
                sentX = activeTranslation.x;
                pendingPx += dx * root.map.stripExtentPx / mini.width;
            }
        }

        FrameAnimation {
            running: lensDrag.active && Math.abs(lensDrag.pendingPx) >= 1
            onTriggered: lensDrag.flush()
        }

        // Drop proxy: re-registered whenever the miniature's screen rect
        // or the cells change, one daemon call per event-loop turn. The
        // bar sits at the screen's top edge with no inset (A2 §3.2), so
        // window coordinates are screen coordinates.
        onXChanged: proxy.restart()
        onYChanged: proxy.restart()
        onWidthChanged: proxy.restart()
        onHeightChanged: proxy.restart()
    }

    Timer {
        id: proxy

        interval: 0
        repeat: false
        onTriggered: root._registerProxy()
    }

    Connections {
        target: root.map

        function onChanged() {
            proxy.restart();
        }
    }
    onMapChanged: proxy.restart()
    Component.onCompleted: proxy.restart()
    Component.onDestruction: {
        if (root.map)
            root.map.unregisterDropProxy();
    }

    function _registerProxy() {
        if (!root.map)
            return;
        if (!root.visible || mini.width <= 0 || mini.height <= 0) {
            root.map.unregisterDropProxy();
            return;
        }
        const p = mini.mapToItem(null, 0, 0);
        const rect = Qt.rect(Math.round(p.x), Math.round(p.y), Math.round(mini.width), Math.round(mini.height));
        root.map.registerDropProxy(rect, DropProxy.cellRects(rect, root.map.cells));
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
