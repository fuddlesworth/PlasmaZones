// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Geometry tests for the connected-corner path (ConnectorGeometry via
// BarCanvas.pathData). The generated SVG path string is deterministic, so
// these pin the structural contract: how many arcs the outline has, that
// a socket adds exactly one concave (inverted) corner per wall, and that
// a zero-depth socket collapses to a flat edge. Visual correctness (the
// shape matching the mockups) is for the bar-canvas demo; this guards the
// math the morph animation drives.

import QtQuick
import QtTest
import Phosphor.Widgets

TestCase {
    id: testCase

    name: "ConnectedCornerGeometry"

    Component {
        id: barComp

        BarCanvas {}
    }

    Component {
        id: cornerComp

        ConnectedCorner {}
    }

    // Count non-overlapping occurrences of sub in s.
    function _count(s, sub) {
        return s.split(sub).length - 1;
    }

    function test_plain_bar_has_four_outer_corners() {
        const b = createTemporaryObject(barComp, testCase, {
            "width": 1000,
            "barHeight": 48,
            "cornerRadius": 14,
            "connectorRadius": 14
        });
        verify(b, "BarCanvas instantiates");
        const d = b.pathData;
        verify(d.length > 0, "path is non-empty");
        compare(_count(d, "A "), 4, "a socketless bar has exactly four (outer) corner arcs");
        compare(_count(d, " 0 0 0 "), 0, "no concave (inverted) corners without a socket");
        verify(d.indexOf("Z") >= 0, "path is closed");
    }

    function test_open_socket_adds_a_pocket() {
        const b = createTemporaryObject(barComp, testCase, {
            "width": 1000,
            "barHeight": 48,
            "cornerRadius": 14,
            "connectorRadius": 14
        });
        b.sockets = [
            {
                "x": 400,
                "width": 200,
                "depth": 300
            }
        ];
        const d = b.pathData;
        // 4 outer + 4 pocket (2 concave top + 2 convex floor) corners.
        compare(_count(d, "A "), 8, "an open socket adds four pocket-corner arcs");
        // Both inverted (concave) top corners use sweep flag 0; the convex
        // pocket-floor corners and all four outer corners use sweep 1.
        // Walking the outline clockwise, a concave corner turns left
        // (sweep 0) and a convex corner turns right (sweep 1).
        compare(_count(d, " 0 0 0 "), 2, "both inverted corners are sweep-0 arcs");
        compare(_count(d, " 0 0 1 "), 6, "four outer + two convex pocket corners are sweep-1 arcs");
    }

    function test_zero_depth_socket_collapses_to_flat_edge() {
        const b = createTemporaryObject(barComp, testCase, {
            "width": 1000,
            "barHeight": 48,
            "cornerRadius": 14,
            "connectorRadius": 14
        });
        b.sockets = [
            {
                "x": 400,
                "width": 200,
                "depth": 0
            }
        ];
        compare(_count(b.pathData, "A "), 4, "a closed (zero-depth) socket leaves the bottom edge flat");
    }

    function test_socket_reserves_pocket_height() {
        const b = createTemporaryObject(barComp, testCase, {
            "width": 1000,
            "barHeight": 48
        });
        compare(b.maxSocketDepth, 0, "no sockets means no extra depth");
        b.sockets = [
            {
                "x": 400,
                "width": 200,
                "depth": 250
            }
        ];
        compare(b.maxSocketDepth, 250, "maxSocketDepth tracks the deepest socket");
        compare(b.implicitHeight, 48 + 250, "implicitHeight reserves the pocket depth");
    }

    function test_connected_corner_convex_path() {
        const c = createTemporaryObject(cornerComp, testCase, {
            "radius": 16,
            "concave": false
        });
        verify(c, "ConnectedCorner instantiates");
        // A quarter-disc pie centred on the origin (sweep 1, convex).
        compare(c._path, "M 0 0 L 16 0 A 16 16 0 0 1 0 16 Z");
    }

    function test_connected_corner_concave_path() {
        const c = createTemporaryObject(cornerComp, testCase, {
            "radius": 16,
            "concave": true
        });
        // The r x r box minus a quarter-disc bite from the origin corner.
        compare(c._path, "M 16 0 L 16 16 L 0 16 A 16 16 0 0 1 16 0 Z");
    }
}
