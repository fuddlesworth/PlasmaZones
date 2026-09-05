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
// Cells are keyed by id. A cell that appears enters (scale 0.7 → 1 over
// 180 ms on the reveal curve); a cell that goes releases (opacity to 0
// over release_long) and is then dropped; a cell that stays retargets its
// geometry. The model's `cells` list is diffed into an internal ListModel
// on every `changed()` so the delegates persist across updates and those
// transitions can run.
//
// Model contract (a PlacementMapScreen QObject): int mode (0 snapping,
// 1 tiling, 2 scrolling, -1 none), real aspect, var cells (list of {id,
// x, y, w, h, t, occupied, focused, label, stack, stripT, columnIndex}
// with x/y/w/h as fractions of the map), var lens ({x, w} fractions,
// scrolling only), int overflowLeft, int overflowRight (columns wholly
// off either end of the lens, from the strip model), int stripExtentPx,
// int desktopCount, int currentDesktop, signal changed(), activate(id),
// scrollView(delta), scrollViewByPx(px), switchDesktop(index). The
// miniature reads none of the pixel members; the bar's lens drag does.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property var model: null
    property real cellRadius: Tokens.radius_mini
    property bool showLens: true
    property bool interactive: false

    signal cellClicked(string id)

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

    ListModel {
        id: cellModel
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
            "label": c.label === undefined || c.label === null ? "" : String(c.label),
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
    // delegate can release before _purge drops the row.
    function _sync() {
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
    }

    function _purge(id) {
        const at = _indexOf(id);
        if (at >= 0 && cellModel.get(at).retiring)
            cellModel.remove(at);
        _retiring = _retiring.filter(x => x !== id);
    }

    // Work-area outline.
    Rectangle {
        anchors.fill: parent
        radius: root.cellRadius
        color: "transparent"
        border.width: 1
        border.color: Qt.rgba(Theme.on_surface.r, Theme.on_surface.g, Theme.on_surface.b, 0.14)
    }

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
            required property string label
            required property bool retiring

            readonly property color _hue: Spectrum.at(t)

            x: cx * root.width
            y: cy * root.height
            width: cw * root.width
            height: ch * root.height
            radius: root.cellRadius
            color: occupied ? Qt.rgba(_hue.r, _hue.g, _hue.b, focused ? 0.55 : 0.40) : "transparent"
            border.width: 1
            border.color: focused ? Spectrum.focus : Qt.rgba(_hue.r, _hue.g, _hue.b, occupied ? 0.8 : 0.3)
            transformOrigin: Item.Center

            Accessible.role: Accessible.Button
            Accessible.name: label

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

            TapHandler {
                enabled: root.interactive && !cell.retiring
                onTapped: root.cellClicked(cell.cellId)
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
}
