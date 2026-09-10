// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// The largest-empty-region function: no cells gives the whole area, a
// two-column layout leaves its free right third, a grid with one cell
// missing gives that gap, full coverage gives nothing, and the result is
// the LARGEST gap rather than the first one found.

import QtQuick
import QtTest
import "qrc:/qt/qml/Phosphor/Lock/LockRegion.js" as Region

TestCase {
    id: testCase

    name: "LockRegion"

    function near(a, b) {
        return Math.abs(a - b) < 1e-6;
    }

    function verifyRect(r, x, y, w, h) {
        verify(r !== null, "expected a rect, got null");
        verify(near(r.x, x), "x " + r.x + " != " + x);
        verify(near(r.y, y), "y " + r.y + " != " + y);
        verify(near(r.width, w), "width " + r.width + " != " + w);
        verify(near(r.height, h), "height " + r.height + " != " + h);
    }

    function test_noCellsIsWholeArea() {
        verifyRect(Region.largestEmptyRect([], 1920, 1080), 0, 0, 1920, 1080);
        verifyRect(Region.largestEmptyRect(null, 1, 1), 0, 0, 1, 1);
    }

    function test_degenerateAreaIsNull() {
        compare(Region.largestEmptyRect([], 0, 100), null);
        compare(Region.largestEmptyRect([], 100, -1), null);
    }

    function test_twoColumnsLeaveTheRightThird() {
        const cells = [
            {
                "x": 0,
                "y": 0,
                "w": 1 / 3,
                "h": 1
            },
            {
                "x": 1 / 3,
                "y": 0,
                "w": 1 / 3,
                "h": 1
            }
        ];
        verifyRect(Region.largestEmptyRect(cells, 1, 1), 2 / 3, 0, 1 / 3, 1);
    }

    function test_twoColumnsInPixels() {
        const cells = [
            {
                "x": 0,
                "y": 0,
                "w": 640,
                "h": 1080
            },
            {
                "x": 640,
                "y": 0,
                "w": 640,
                "h": 1080
            }
        ];
        verifyRect(Region.largestEmptyRect(cells, 1920, 1080), 1280, 0, 640, 1080);
    }

    function test_gridWithAMissingCellGivesThatGap() {
        // 2 x 2 with 2 % gutters, bottom-right cell missing.
        const g = 0.02;
        const s = (1 - 3 * g) / 2;
        const cells = [
            {
                "x": g,
                "y": g,
                "w": s,
                "h": s
            },
            {
                "x": 2 * g + s,
                "y": g,
                "w": s,
                "h": s
            },
            {
                "x": g,
                "y": 2 * g + s,
                "w": s,
                "h": s
            }
        ];
        // The gap is the missing quadrant plus the gutters around it: from
        // the top-right cell's bottom edge and the bottom-left cell's right
        // edge to the area's corner.
        verifyRect(Region.largestEmptyRect(cells, 1, 1), g + s, g + s, 1 - g - s, 1 - g - s);
    }

    function test_fullCoverageIsNull() {
        const cells = [
            {
                "x": 0,
                "y": 0,
                "w": 0.5,
                "h": 0.5
            },
            {
                "x": 0.5,
                "y": 0,
                "w": 0.5,
                "h": 0.5
            },
            {
                "x": 0,
                "y": 0.5,
                "w": 0.5,
                "h": 0.5
            },
            {
                "x": 0.5,
                "y": 0.5,
                "w": 0.5,
                "h": 0.5
            }
        ];
        compare(Region.largestEmptyRect(cells, 1, 1), null);
    }

    function test_largestGapWinsOverTheFirst() {
        // A thin free band on the left (0.1 wide) and a wide one on the
        // right (0.3 wide): the right one is larger and must win even
        // though the scan meets the left one first.
        const cells = [
            {
                "x": 0.1,
                "y": 0,
                "w": 0.6,
                "h": 1
            }
        ];
        verifyRect(Region.largestEmptyRect(cells, 1, 1), 0.7, 0, 0.3, 1);
    }

    function test_cellsOutsideTheAreaAreClipped() {
        const cells = [
            {
                "x": -0.5,
                "y": -0.5,
                "w": 1,
                "h": 2
            }
        ];
        verifyRect(Region.largestEmptyRect(cells, 1, 1), 0.5, 0, 0.5, 1);
    }

    function test_emptyCellsAreIgnored() {
        const cells = [
            {
                "x": 0.5,
                "y": 0.5,
                "w": 0,
                "h": 0.2
            },
            {
                "x": 0.2,
                "y": 0.2,
                "w": 0.3,
                "h": 0
            }
        ];
        verifyRect(Region.largestEmptyRect(cells, 1, 1), 0, 0, 1, 1);
    }

    function test_freeRegionBetweenRows() {
        // Two full-width rows with a free band between them.
        const cells = [
            {
                "x": 0,
                "y": 0,
                "w": 1,
                "h": 0.3
            },
            {
                "x": 0,
                "y": 0.7,
                "w": 1,
                "h": 0.3
            }
        ];
        verifyRect(Region.largestEmptyRect(cells, 1, 1), 0, 0.3, 1, 0.4);
    }
}
