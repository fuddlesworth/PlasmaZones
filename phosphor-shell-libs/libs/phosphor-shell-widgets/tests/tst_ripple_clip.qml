// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Pixel regression for PhosphorRipple's rounded corners.
//
// The press ripple used to be a growing circle behind Item.clip. Item.clip
// is a rectangular scissor, so on a pill the sweep squared off the rounded
// ends for the few hundred ms it took to cross them. The ripple is now a
// radial gradient painted inside a rounded-rect Shape, so the corners are
// exact by construction.
//
// This is the one visual claim worth pinning in the harness rather than
// leaving to the kitchen-sink demo, because the failure is transient (it
// only shows mid-sweep) and therefore easy to reintroduce unnoticed.
// Verified non-vacuous: forcing the Shape's outline back to radius 0
// reproduces the bleed and fails this case.

import QtQuick
import QtTest
import Phosphor.Widgets

TestCase {
    id: testCase

    name: "PhosphorRippleClip"
    when: windowShown
    visible: true
    width: 200
    height: 60

    Rectangle {
        id: host

        anchors.fill: parent
        // Opaque backdrop so any ripple ink reads as a non-black pixel.
        color: "black"

        PhosphorRipple {
            id: ripple

            anchors.fill: parent
            radius: height / 2
            rippleColor: "white"
        }
    }

    // (1,1) lies outside a 200x60 capsule of radius 30: its distance from
    // the corner arc's centre (30,30) is ~41 > 30. It is inside the
    // bounding box, which is exactly why the old rectangular clip let the
    // circle cover it.
    function test_capsule_corner_takes_no_ripple_ink() {
        const black = Qt.rgba(0, 0, 0, 1);
        ripple.start(150, 30);

        // Sample across the whole sweep rather than at one instant: the
        // disc only passes over the corner partway through, so a single
        // late grab would miss the bleed entirely.
        let sawInk = false;
        let sawRipple = false;
        for (let i = 0; i < 12; ++i) {
            wait(25);
            const frame = grabImage(host);
            if (frame.pixel(1, 1) !== black)
                sawInk = true;
            if (frame.pixel(150, 30) !== black)
                sawRipple = true;
        }

        // Guards the guard: if the ripple never painted at all, the corner
        // being clean would prove nothing.
        verify(sawRipple, "the ripple paints at the press point");
        verify(!sawInk, "the capsule corner never takes ripple ink");
    }
}
