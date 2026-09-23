// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// The placement map's drop proxy (A2 §1.5): the cell rects the bar hands
// the daemon are the model's 0..1 fractions scaled onto the miniature's
// screen rect, whole pixels, and cells with no id or no area are left
// out. The JSON itself is built and tested in PhosphorShell
// (PlacementMapParser::dropProxyJson); this covers the bar's half.

import QtQuick
import QtTest
import "qrc:/qt/qml/Phosphor/Bar/DropProxyGeometry.js" as DropProxy

TestCase {
    id: testCase

    name: "DropProxyGeometry"

    function test_cells_scale_onto_the_miniature_rect() {
        const rect = Qt.rect(100, 4, 32, 18);
        const cells = [
            {
                "id": "left",
                "x": 0,
                "y": 0,
                "w": 0.5,
                "h": 1
            },
            {
                "id": "right-top",
                "x": 0.5,
                "y": 0,
                "w": 0.5,
                "h": 0.5
            }
        ];
        const out = DropProxy.cellRects(rect, cells);
        compare(out.length, 2);
        compare(out[0].id, "left");
        compare(out[0].x, 100);
        compare(out[0].y, 4);
        compare(out[0].w, 16);
        compare(out[0].h, 18);
        compare(out[1].id, "right-top");
        compare(out[1].x, 116);
        compare(out[1].y, 4);
        compare(out[1].w, 16);
        compare(out[1].h, 9);
    }

    function test_unnamed_and_empty_cells_are_dropped() {
        const rect = Qt.rect(0, 0, 40, 20);
        const out = DropProxy.cellRects(rect, [
            {
                "id": "",
                "x": 0,
                "y": 0,
                "w": 1,
                "h": 1
            },
            {
                "id": "thin",
                "x": 0.5,
                "y": 0,
                "w": 0.001,
                "h": 1
            },
            {
                "id": "ok",
                "x": 0,
                "y": 0,
                "w": 1,
                "h": 1
            }
        ]);
        compare(out.length, 1);
        compare(out[0].id, "ok");
        compare(out[0].w, 40);
    }

    function test_empty_rect_or_model_gives_nothing() {
        compare(DropProxy.cellRects(Qt.rect(0, 0, 0, 18), [
            {
                "id": "a",
                "x": 0,
                "y": 0,
                "w": 1,
                "h": 1
            }
        ]).length, 0);
        compare(DropProxy.cellRects(Qt.rect(0, 0, 32, 18), null).length, 0);
        compare(DropProxy.cellRects(null, []).length, 0);
    }
}
