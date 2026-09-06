// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.PlacementMiniature, the placement engine's map in
// miniature.
//
// Draws one screen's placement state from a PlacementMapScreen model:
// the work-area outline, one cell per zone / tile / column in the ramp
// colour of its `t`, the white lens over the visible part of a scrolling
// strip, and 1 px gutter columns at the ends for the columns scrolled
// off either side. This is the bar's live map, the launcher's viewfinder
// and the dashboard's grid cell, so it is pure QML over the model and
// nothing else (05 §2, claim 1).
//
// The map is drawn in two layers. FILLS are per cell, keyed by id: a cell
// that appears enters (scale 0.7 → 1 over 180 ms on the reveal curve), a
// cell that goes releases (opacity to 0 over release_long) and is then
// dropped, and a cell that stays retargets its geometry. EDGES are the
// cell set's lines, kept in their own persistent model and reconciled
// through MiniatureEdges.js on every `changed()`: an edge within 6 % of
// an existing edge of the same orientation retargets, a new edge enters
// (opacity over 180 ms, staggered 15 ms in reading order, capped 150 ms),
// a vanished edge releases (720 ms) and is dropped, and at most two
// generations are drawn, the live one and the releasing one (A2 §2).
// That is what makes a mode morph read as one shape: the 50/50 split
// shared by a snapping layout and a tiling result is handed over, never
// redrawn. `morphing` is true while any edge is entering or releasing.
//
// Cell states (A2 §1.4, §6): focused = white edge + a 2 px white core
// line along the top; hovered = +10 % lightness, edge at 100 %; pressed =
// fill at 55 %; urgent = white edge pulsing 0.4 → 1.0 at 1.2 s (steady
// under reduced motion); a drag's target = white edge. Labels (the app
// icon, or the title's first letter) draw only with `labels: true`, the
// expanded height.
//
// Interactions, with `interactive: true`, are reported as signals and the
// host routes each through the model so nothing bypasses the mode router:
// click → cellClicked (activate), drag a cell onto another → cellMoved
// (moveCell; a 60 % copy follows the pointer in its hue), middle-click →
// cellFloatToggled (toggleFloat), right-click → contextMenuRequested with
// the point in this item's coordinates.
//
// Model contract (a PlacementMapScreen QObject): int mode (0 snapping,
// 1 tiling, 2 scrolling, -1 none), real aspect, var cells (list of {id,
// x, y, w, h, t, occupied, focused, urgent, label, appId, title,
// windowId, stack, stripT, columnIndex} with x/y/w/h as fractions of the
// map), var lens ({x, w} fractions, scrolling only), int overflowLeft,
// int overflowRight (columns wholly off either end of the lens), int
// stripExtentPx, int desktopCount, int currentDesktop, signal changed().
// The miniature reads none of the pixel members; the bar's lens drag does.

import QtQuick
import org.kde.kirigami as Kirigami
import Phosphor.Theme
import "MiniatureEdges.js" as Edges

Item {
    id: root

    property var model: null
    property real cellRadius: Tokens.radius_mini
    property bool showLens: true
    property bool interactive: false
    // Draw the app glyph (or the title's first letter) in each occupied
    // cell. Only at expanded height (A2 §1.3).
    property bool labels: false

    signal cellClicked(string id)
    signal cellMoved(string fromId, string toId)
    signal cellFloatToggled(string id)
    // `x`/`y` in this item's coordinates; `id` is empty off any cell.
    signal contextMenuRequested(string id, real x, real y)

    // Aspect-true: the map is as wide as it is given and as tall as the
    // screen's aspect says. A model-less miniature draws as 16:9.
    readonly property real aspect: model && model.aspect > 0 ? model.aspect : 16 / 9
    implicitWidth: 64
    implicitHeight: implicitWidth / aspect

    readonly property int _overflowLeft: model ? model.overflowLeft : 0
    readonly property int _overflowRight: model ? model.overflowRight : 0
    readonly property bool _scrolling: model ? model.mode === 2 : false
    readonly property var _lens: model && model.lens ? model.lens : null
    // Live cell count, for hosts and tests. Retiring cells are excluded.
    readonly property int liveCount: cellModel.count - _retiring.length
    // Ids currently releasing, kept out of `liveCount` and re-adopted if
    // the id comes back before the release finishes.
    property var _retiring: []

    // The edge layer, for tests: rows are {key, o, pos, start, end,
    // occupied, phase ("enter" | "live" | "release"), delay}.
    readonly property alias edgeModel: edgeModel
    // True while any edge is entering or releasing (A2 §2.1 phases).
    readonly property bool morphing: _enteringEdges > 0 || _releasingEdges > 0
    property int _enteringEdges: 0
    property int _releasingEdges: 0

    // The drag in flight: the dragged cell, the pointer in map
    // coordinates, and the cell under it.
    property string _dragId: ""
    property real _dragX: 0
    property real _dragY: 0
    property string dropTargetId: ""

    ListModel {
        id: cellModel
    }

    ListModel {
        id: edgeModel
    }

    Connections {
        target: root.model
        function onChanged() {
            root._sync();
        }
    }
    onModelChanged: _sync()
    Component.onCompleted: _sync()

    // Structure-axis hue for a gutter column. Columns off either end sit at
    // the ends of the strip, so the k-th off-left column is near cyan and
    // the k-th off-right column near rose.
    function _gutterT(index, side) {
        const total = _overflowLeft + liveCount + _overflowRight;
        if (total <= 1)
            return side < 0 ? 0 : 1;
        const pos = side < 0 ? index : _overflowLeft + liveCount + index;
        return pos / (total - 1);
    }

    function _str(v) {
        return v === undefined || v === null ? "" : String(v);
    }

    function _row(c) {
        return {
            "cellId": String(c.id),
            "cx": Number(c.x) || 0,
            "cy": Number(c.y) || 0,
            "cw": Number(c.w) || 0,
            "ch": Number(c.h) || 0,
            "t": Number(c.t) || 0,
            "occupied": !!c.occupied,
            "focused": !!c.focused,
            "urgent": !!c.urgent,
            "label": _str(c.label),
            "appId": _str(c.appId),
            "title": _str(c.title),
            "retiring": false
        };
    }

    function _indexOf(id) {
        for (let i = 0; i < cellModel.count; ++i) {
            if (cellModel.get(i).cellId === id)
                return i;
        }
        return -1;
    }

    // Diff the model's cell list into cellModel: update rows in place,
    // append new ids, and mark rows whose id is gone as retiring so the
    // delegate can release before _purge drops the row. Then reconcile
    // the edge layer against the new geometry.
    // False once the engine is tearing down (the theme singletons are gone
    // by then). The map is C++ and outlives the engine, and the delegates'
    // own animations finish during teardown too, so a model write can be
    // asked for while the miniature is dying; creating the delegates'
    // deferred animations on a dying context then is a crash, not a
    // TypeError. Every model write checks this first. Nothing to draw.
    // Set at the start of this item's own destruction, which runs before
    // its children (the delegates and their animations) are torn down.
    property bool _dying: false
    Component.onDestruction: root._dying = true

    function _engineAlive() {
        return !root._dying && typeof Motion !== "undefined" && Motion !== null && typeof Theme !== "undefined" && Theme !== null;
    }

    function _sync() {
        if (!_engineAlive())
            return;
        const cells = model && model.cells ? model.cells : [];
        const seen = {};
        for (let i = 0; i < cells.length; ++i) {
            const row = _row(cells[i]);
            seen[row.cellId] = true;
            const at = _indexOf(row.cellId);
            if (at < 0)
                cellModel.append(row);
            else
                cellModel.set(at, row);
        }
        const retiring = [];
        for (let i = 0; i < cellModel.count; ++i) {
            const r = cellModel.get(i);
            if (!seen[r.cellId]) {
                cellModel.setProperty(i, "retiring", true);
                retiring.push(r.cellId);
            }
        }
        _retiring = retiring;
        if (_dragId !== "" && !seen[_dragId])
            _endDrag(false);
        _syncEdges(cells);
    }

    function _purge(id) {
        if (!_engineAlive())
            return;
        const at = _indexOf(id);
        if (at >= 0 && cellModel.get(at).retiring)
            cellModel.remove(at);
        _retiring = _retiring.filter(x => x !== id);
    }

    // ─── Edge layer ─────────────────────────────────────────────────────
    function _edgeIndex(key) {
        for (let i = 0; i < edgeModel.count; ++i) {
            if (edgeModel.get(i).key === key)
                return i;
        }
        return -1;
    }

    function _syncEdges(cells) {
        const existing = [];
        for (let i = 0; i < edgeModel.count; ++i) {
            const e = edgeModel.get(i);
            existing.push({
                "key": e.key,
                "o": e.o,
                "pos": e.pos,
                "start": e.start,
                "end": e.end,
                "releasing": e.phase === "release"
            });
        }
        const r = Edges.reconcile(existing, Edges.edgesFor(cells));
        // Oldest generation first: gone at once (A2 §2.3).
        for (const key of r.drop) {
            const at = _edgeIndex(key);
            if (at >= 0) {
                edgeModel.remove(at);
                _releasingEdges = Math.max(0, _releasingEdges - 1);
            }
        }
        for (const t of r.retarget) {
            const at = _edgeIndex(t.key);
            if (at < 0)
                continue;
            const e = edgeModel.get(at);
            if (e.phase === "release") {
                // Re-adopted before its release finished: live again.
                edgeModel.setProperty(at, "phase", "live");
                _releasingEdges = Math.max(0, _releasingEdges - 1);
            }
            edgeModel.setProperty(at, "pos", t.pos);
            edgeModel.setProperty(at, "start", t.start);
            edgeModel.setProperty(at, "end", t.end);
            edgeModel.setProperty(at, "occupied", t.occupied);
        }
        for (const key of r.release) {
            const at = _edgeIndex(key);
            if (at >= 0 && edgeModel.get(at).phase !== "release") {
                if (edgeModel.get(at).phase === "enter")
                    _enteringEdges = Math.max(0, _enteringEdges - 1);
                edgeModel.setProperty(at, "phase", "release");
                _releasingEdges += 1;
            }
        }
        for (const n of r.enter) {
            edgeModel.append({
                "key": n.key,
                "o": n.o,
                "pos": n.pos,
                "start": n.start,
                "end": n.end,
                "occupied": n.occupied,
                "phase": "enter",
                "delay": n.delay
            });
            _enteringEdges += 1;
        }
    }

    function _edgeEntered(key) {
        if (!_engineAlive())
            return;
        const at = _edgeIndex(key);
        if (at >= 0 && edgeModel.get(at).phase === "enter") {
            edgeModel.setProperty(at, "phase", "live");
            _enteringEdges = Math.max(0, _enteringEdges - 1);
        }
    }

    function _edgeReleased(key) {
        if (!_engineAlive())
            return;
        const at = _edgeIndex(key);
        if (at >= 0 && edgeModel.get(at).phase === "release") {
            edgeModel.remove(at);
            _releasingEdges = Math.max(0, _releasingEdges - 1);
        }
    }

    // ─── Drag ───────────────────────────────────────────────────────────
    // The live cell under a point in map coordinates, other than `except`.
    function _cellAt(px, py, except) {
        const fx = root.width > 0 ? px / root.width : -1;
        const fy = root.height > 0 ? py / root.height : -1;
        for (let i = 0; i < cellModel.count; ++i) {
            const c = cellModel.get(i);
            if (c.retiring || c.cellId === except)
                continue;
            if (fx >= c.cx && fx < c.cx + c.cw && fy >= c.cy && fy < c.cy + c.ch)
                return c.cellId;
        }
        return "";
    }

    function _updateDrag(id, px, py) {
        _dragId = id;
        _dragX = px;
        _dragY = py;
        dropTargetId = _cellAt(px, py, id);
    }

    function _endDrag(commit) {
        const from = _dragId;
        const to = dropTargetId;
        _dragId = "";
        dropTargetId = "";
        if (commit && from !== "" && to !== "" && from !== to)
            root.cellMoved(from, to);
    }

    // Work-area outline.
    Rectangle {
        anchors.fill: parent
        radius: root.cellRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, 0.14)
    }

    // Right-click off any cell.
    TapHandler {
        enabled: root.interactive
        acceptedButtons: Qt.RightButton
        onTapped: (eventPoint, button) => root.contextMenuRequested("", eventPoint.position.x, eventPoint.position.y)
    }

    // ─── Fills ──────────────────────────────────────────────────────────
    Repeater {
        id: cells

        model: cellModel
        delegate: Rectangle {
            id: cell

            required property int index
            required property string cellId
            required property real cx
            required property real cy
            required property real cw
            required property real ch
            required property real t
            required property bool occupied
            required property bool focused
            required property bool urgent
            required property string label
            required property string appId
            required property string title
            required property bool retiring

            readonly property color _hue: Spectrum.at(t)
            readonly property bool _hovered: hover.hovered && root.interactive && !retiring
            readonly property bool _pressed: tap.pressed || drag.active
            readonly property bool _dropTarget: root.dropTargetId === cellId
            readonly property color _lit: _hovered ? Qt.lighter(_hue, 1.1) : _hue
            // Urgent pulse, 0.4 → 1.0 at 1.2 s; steady under reduced motion.
            property real _pulse: 1

            x: cx * root.width
            y: cy * root.height
            width: cw * root.width
            height: ch * root.height
            radius: root.cellRadius
            color: occupied ? Qt.rgba(_lit.r, _lit.g, _lit.b, _pressed || focused ? 0.55 : 0.40) : "transparent"
            // The resting hue edge is the edge layer's; the fill carries
            // only the white signals and the hover lift.
            border.width: _dropTarget || urgent || focused || _hovered ? 1 : 0
            border.color: _dropTarget ? Spectrum.focus : urgent ? Qt.rgba(1, 1, 1, _pulse) : focused ? Qt.rgba(1, 1, 1, 0.9) : _lit
            transformOrigin: Item.Center

            Accessible.role: Accessible.Button
            Accessible.name: title !== "" ? label + ": " + title : label

            SequentialAnimation on _pulse {
                running: cell.urgent && !Motion.reducedMotion && !cell.retiring
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
                        cell._pulse = 1;
                }
            }

            // Focus: a 2 px white core line along the top edge.
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 1
                height: 2
                radius: 1
                color: Spectrum.focus
                opacity: cell.focused ? 0.9 : 0
                Behavior on opacity {
                    NumberAnimation {
                        duration: cell.focused ? Motion.duration_enter : Motion.duration_release
                        easing: cell.focused ? Motion.enter : Motion.release
                    }
                }
            }

            // Label at expanded height: the app glyph, or the title's first
            // letter when no icon resolves.
            Kirigami.Icon {
                id: glyph

                anchors.centerIn: parent
                width: 12
                height: 12
                visible: root.labels && cell.occupied && valid && source !== "" && cell.width >= 16 && cell.height >= 16
                source: cell.appId
                color: Theme.on_surface
            }
            TabularText {
                anchors.centerIn: parent
                visible: root.labels && cell.occupied && !glyph.visible && text !== "" && cell.width >= 12 && cell.height >= 12
                text: cell.title.length > 0 ? cell.title.charAt(0).toUpperCase() : ""
                font.pixelSize: Tokens.font_size_label_s
                opacity: 0.9
            }

            // Enter: scale 0.7 → 1 on the reveal curve.
            scale: 0.7
            Component.onCompleted: scale = 1
            Behavior on scale {
                NumberAnimation {
                    duration: 180
                    easing: Motion.reveal
                }
            }

            // Geometry retargets from the current value (R6).
            Behavior on x {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on y {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on width {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on height {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }

            // Release: fade out over release_long, then drop the row.
            opacity: retiring ? 0 : 1
            Behavior on opacity {
                SequentialAnimation {
                    NumberAnimation {
                        duration: Motion.duration_release_long
                        easing: Motion.release
                    }
                    ScriptAction {
                        script: {
                            if (cell.retiring)
                                root._purge(cell.cellId);
                        }
                    }
                }
            }

            HoverHandler {
                id: hover

                enabled: root.interactive && !cell.retiring
                cursorShape: Qt.PointingHandCursor
            }
            TapHandler {
                id: tap

                enabled: root.interactive && !cell.retiring
                acceptedButtons: Qt.LeftButton
                onTapped: root.cellClicked(cell.cellId)
            }
            TapHandler {
                enabled: root.interactive && !cell.retiring
                acceptedButtons: Qt.MiddleButton
                onTapped: root.cellFloatToggled(cell.cellId)
            }
            TapHandler {
                enabled: root.interactive && !cell.retiring
                acceptedButtons: Qt.RightButton
                onTapped: eventPoint => {
                    const p = cell.mapToItem(root, eventPoint.position.x, eventPoint.position.y);
                    root.contextMenuRequested(cell.cellId, p.x, p.y);
                }
            }
            // Drag onto another cell. The cell itself stays put; a 60 %
            // copy follows the pointer (the ghost below).
            DragHandler {
                id: drag

                enabled: root.interactive && !cell.retiring && cell.occupied
                target: null
                onActiveChanged: {
                    if (active) {
                        const p = cell.mapToItem(root, centroid.position.x, centroid.position.y);
                        root._updateDrag(cell.cellId, p.x, p.y);
                    } else if (root._dragId === cell.cellId) {
                        root._endDrag(true);
                    }
                }
                onCentroidChanged: {
                    if (!active)
                        return;
                    const p = cell.mapToItem(root, centroid.position.x, centroid.position.y);
                    root._updateDrag(cell.cellId, p.x, p.y);
                }
            }
        }
    }

    // ─── Edges ──────────────────────────────────────────────────────────
    Repeater {
        model: edgeModel
        delegate: Rectangle {
            id: edge

            required property int index
            required property string key
            required property string o
            required property real pos
            required property real start
            required property real end
            required property bool occupied
            required property string phase
            required property int delay

            readonly property bool _v: o === "v"
            // Hue at the edge's own position on the rail axis.
            readonly property color _hue: Spectrum.at(_v ? pos : (start + end) / 2)
            readonly property real _target: occupied ? 0.8 : 0.3

            x: _v ? Math.round(pos * root.width) : Math.round(start * root.width)
            y: _v ? Math.round(start * root.height) : Math.round(pos * root.height)
            width: _v ? 1 : Math.max(1, Math.round((end - start) * root.width))
            height: _v ? Math.max(1, Math.round((end - start) * root.height)) : 1
            color: _hue

            // Enter from 0; release to 0 and drop; retarget in between.
            opacity: 0
            Component.onCompleted: opacity = Qt.binding(() => edge.phase === "release" ? 0 : edge._target)
            Behavior on opacity {
                SequentialAnimation {
                    PauseAnimation {
                        duration: edge.phase === "enter" ? edge.delay : 0
                    }
                    NumberAnimation {
                        duration: edge.phase === "release" ? (Motion.reducedMotion ? Motion.duration_release : Motion.duration_release_long) : 180
                        easing: edge.phase === "release" ? Motion.release : Motion.reveal
                    }
                    ScriptAction {
                        script: {
                            if (edge.phase === "release")
                                root._edgeReleased(edge.key);
                            else if (edge.phase === "enter")
                                root._edgeEntered(edge.key);
                        }
                    }
                }
            }

            Behavior on x {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on y {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on width {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
            Behavior on height {
                NumberAnimation {
                    duration: Motion.duration_release
                    easing: Motion.release
                }
            }
        }
    }

    // Scrolling: the viewport lens, white at 70 %.
    Rectangle {
        visible: root.showLens && root._scrolling && root._lens !== null
        x: root._lens ? (Number(root._lens.x) || 0) * root.width : 0
        width: root._lens ? (Number(root._lens.w) || 0) * root.width : 0
        y: 0
        height: root.height
        radius: root.cellRadius
        color: "transparent"
        border.width: 1
        border.color: Spectrum.focus
        opacity: 0.7

        Behavior on x {
            NumberAnimation {
                duration: Motion.duration_release
                easing: Motion.release
            }
        }
        Behavior on width {
            NumberAnimation {
                duration: Motion.duration_release
                easing: Motion.release
            }
        }
    }

    // Gutter columns for the strip's off-screen columns, packed at the
    // ends in their structure-axis hues.
    Repeater {
        model: root._scrolling ? root._overflowLeft : 0
        delegate: Rectangle {
            required property int index
            x: 1 + index * 2
            y: 1
            width: 1
            height: root.height - 2
            color: Spectrum.at(root._gutterT(index, -1))
        }
    }
    Repeater {
        model: root._scrolling ? root._overflowRight : 0
        delegate: Rectangle {
            required property int index
            x: root.width - 2 - (root._overflowRight - 1 - index) * 2
            y: 1
            width: 1
            height: root.height - 2
            color: Spectrum.at(root._gutterT(index, 1))
        }
    }

    // The drag ghost: a 60 % copy of the dragged cell in its hue, under
    // the pointer. Follows 1:1, no easing.
    Rectangle {
        id: ghost

        readonly property int _at: root._dragId !== "" ? root._indexOf(root._dragId) : -1
        readonly property var _row: _at >= 0 ? cellModel.get(_at) : null
        readonly property color _hue: Spectrum.at(_row ? _row.t : 0)

        visible: _row !== null
        width: _row ? _row.cw * root.width : 0
        height: _row ? _row.ch * root.height : 0
        x: root._dragX - width / 2
        y: root._dragY - height / 2
        radius: root.cellRadius
        color: Qt.rgba(_hue.r, _hue.g, _hue.b, 0.55)
        border.width: 1
        border.color: _hue
        opacity: 0.6
        z: 10
    }
}
