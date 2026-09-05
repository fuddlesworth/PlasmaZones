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
        model: root.cellRects
        delegate: Rectangle {
            id: cell

            required property var modelData

            readonly property color _hue: Spectrum.at(modelData.t)

            x: Math.round(modelData.x)
            y: Math.round(modelData.y)
            width: Math.max(1, Math.round(modelData.w))
            height: Math.max(1, Math.round(modelData.h))
            radius: Tokens.radius_edge
            color: Qt.rgba(_hue.r, _hue.g, _hue.b, modelData.occupied ? root.fillOpacity : root.fillOpacity / 2)
            border.width: 1
            border.color: modelData.focused ? Spectrum.focus : Qt.rgba(_hue.r, _hue.g, _hue.b, root.strokeOpacity)

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
