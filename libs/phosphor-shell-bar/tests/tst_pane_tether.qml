// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PaneTether's routing math (A2 §4.3 step 2): a pane under its chip gets a
// straight drop; a pane the engine put elsewhere gets a run along the rail
// to the nearest x inside the pane, then the drop from there.

import QtQuick
import QtTest
import Phosphor.Bar

TestCase {
    id: testCase

    name: "PaneTether"

    Component {
        id: tetherComponent

        PaneTether {
            width: 1920
            height: 500
            screenWidth: 1920
            barThickness: 28
            railThickness: 2
            inset: 16
            progress: 1
        }
    }

    function test_unlocatedPaneDropsStraightToTheBand() {
        const t = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 1700
        });
        verify(t.underChip);
        compare(t.dropX, 1700);
        compare(t.runWidth, 0);
        compare(t.dropTop, 2);
        compare(t.dropBottom, 28);
        compare(t.dropHeight, 26);
    }

    function test_paneUnderTheChipDropsStraightToItsTopEdge() {
        const t = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 1700,
            "paneRect": Qt.rect(1500, 36, 380, 460)
        });
        verify(t.underChip);
        compare(t.dropX, 1700);
        compare(t.runWidth, 0);
        compare(t.dropBottom, 36);
        compare(t.dropHeight, 34);
    }

    function test_paneLeftOfTheChipRoutesAlongTheRail() {
        // Tiling placed the pane as a left tile; the chip is far right.
        const t = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 1700,
            "paneRect": Qt.rect(8, 36, 940, 1040)
        });
        verify(!t.underChip);
        // The nearest x inside the pane, inset from its right edge.
        compare(t.dropX, 8 + 940 - 16);
        compare(t.runStart, 932);
        compare(t.runWidth, 1700 - 932);
        compare(t.dropBottom, 36);
    }

    function test_paneRightOfTheChipRoutesTheOtherWay() {
        const t = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 100,
            "paneRect": Qt.rect(1200, 36, 700, 900)
        });
        verify(!t.underChip);
        compare(t.dropX, 1216);
        compare(t.runStart, 100);
        compare(t.runWidth, 1116);
    }

    function test_narrowPaneClampsTheInsetToItsHalfWidth() {
        const t = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 1700,
            "paneRect": Qt.rect(100, 36, 20, 400)
        });
        compare(t.dropX, 110);
    }

    function test_progressGrowsTheDropOverTheFirstSixtyPercent() {
        const t = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 500,
            "paneRect": Qt.rect(300, 36, 380, 460),
            "progress": 0.25
        });
        fuzzyCompare(t.dropHeight, 34 * 0.4, 0.001);
        t.progress = 0.7;
        compare(t.dropHeight, 34);
        t.progress = 0;
        compare(t.dropHeight, 0);
        verify(!t.visible);
    }

    function test_hueFollowsTheDropXNotTheChip() {
        const under = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 960,
            "paneRect": Qt.rect(800, 36, 380, 460)
        });
        fuzzyCompare(under.hueT, 0.5, 0.001);
        const routed = createTemporaryObject(tetherComponent, testCase, {
            "anchorX": 1900,
            "paneRect": Qt.rect(0, 36, 960, 460)
        });
        verify(routed.hueT < 0.5);
    }
}
