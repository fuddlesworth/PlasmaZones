// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Dashboard.FullMap, the screen's cells at 1:1.
//
// The placement map drawn over the desktop itself: one outline per cell
// at the cell's own screen rect, a 1 px stroke in the cell's spectrum
// hue and an 8 % fill, the focused cell's edge white. The cheatsheet
// hangs its chord labels on these rects (A3 §10 d: labels are positioned
// from the engine's rects), so `cellRects()` hands the same rects out in
// this item's coordinates.
//
// Model contract: a PlacementMapScreen (`workArea` in screen pixels,
// `cells` in work-area fractions, `changed()`), or any object with those
// members. The item is expected to fill the screen, so the work area's
// origin is its own.

import QtQuick
import Phosphor.Theme

Item {
    id: root

    property var model: null
    // Fill and stroke alphas (A3 §10 b: 1 px spectrum stroke, 8 % fill).
    property real fillOpacity: 0.08
    property real strokeOpacity: 0.7

    readonly property rect _area: model && model.workArea ? model.workArea : Qt.rect(0, 0, width, height)
    // The cells resolved to this item's pixels: {id, x, y, w, h,
    // zoneNumber, focused, t}. Rebuilt on the model's coalesced changed().
    property var cellRects: []
    readonly property string focusedId: {
        for (let i = 0; i < cellRects.length; ++i) {
            if (cellRects[i].focused)
                return cellRects[i].id;
        }
        return "";
    }

    function _resolve(): void {
        const cells = model && model.cells ? model.cells : [];
        const a = _area;
        const out = [];
        for (let i = 0; i < cells.length; ++i) {
            const c = cells[i];
            out.push({
                "id": String(c.id),
                "x": a.x + (Number(c.x) || 0) * a.width,
                "y": a.y + (Number(c.y) || 0) * a.height,
                "w": (Number(c.w) || 0) * a.width,
                "h": (Number(c.h) || 0) * a.height,
                "zoneNumber": Number(c.zoneNumber) || 0,
                "focused": !!c.focused,
                "occupied": !!c.occupied,
                "t": Number(c.t) || 0
            });
        }
        cellRects = out;
        _syncModel(out);
    }

    // Update the Repeater's rows in place instead of handing it a fresh array.
    // A JS-array model reassignment destroys and rebuilds every delegate, so
    // the geometry Behaviors below never run and the rects hard-cut on every
    // engine change — the opposite of the "retarget, never jump" they exist
    // for. Rows are matched by id so a cell keeps its delegate across a
    // reorder; PlacementMiniature reconciles the same way.
    function _syncModel(rows): void {
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i];
            let at = -1;
            for (let j = i; j < cellModel.count; ++j) {
                if (cellModel.get(j).cellId === r.id) {
                    at = j;
                    break;
                }
            }
            if (at === -1) {
                cellModel.insert(i, _modelRow(r));
                continue;
            }
            if (at !== i)
                cellModel.move(at, i, 1);
            const row = _modelRow(r);
            for (const key in row)
                cellModel.setProperty(i, key, row[key]);
        }
        while (cellModel.count > rows.length)
            cellModel.remove(cellModel.count - 1);
    }

    function _modelRow(r) {
        return {
            "cellId": r.id,
            "cx": r.x,
            "cy": r.y,
            "cw": r.w,
            "ch": r.h,
            "occupied": r.occupied,
            "focused": r.focused,
            "t": r.t
        };
    }

    ListModel {
        id: cellModel
    }

    Connections {
        target: root.model
        function onChanged() {
            root._resolve();
        }
    }
    onModelChanged: _resolve()
    on_AreaChanged: _resolve()
    Component.onCompleted: _resolve()

    Repeater {
        model: cellModel
        delegate: Rectangle {
            id: cell

            required property real cx
            required property real cy
            required property real cw
            required property real ch
            required property real t
            required property bool occupied
            required property bool focused

            readonly property color _hue: Spectrum.at(cell.t)

            x: Math.round(cell.cx)
            y: Math.round(cell.cy)
            width: Math.max(1, Math.round(cell.cw))
            height: Math.max(1, Math.round(cell.ch))
            radius: Tokens.radius_edge
            color: Qt.rgba(_hue.r, _hue.g, _hue.b, cell.occupied ? root.fillOpacity : root.fillOpacity / 2)
            border.width: 1
            border.color: cell.focused ? Spectrum.focus : Qt.rgba(_hue.r, _hue.g, _hue.b, root.strokeOpacity)

            // Rects follow the engine (live mode): retarget, never jump.
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
}
