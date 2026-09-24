// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Behaviour tests for the bar panels' shared chrome: PanelFrame's sizing
// contract and PanelRow's press contract.
//
// Only the two service-free atoms are covered. The panels themselves
// (NetworkPanel and the rest) import Phosphor.Service.* modules that only
// the shell process registers, so they cannot be instantiated here for the
// same reason the real bar widgets cannot — the same boundary tst_slot.qml
// works around with a fake registry.
//
// PanelFrame's sizing is worth pinning because a popout is measured by its
// content's IMPLICIT size: a frame that reported zero, or that grew without
// bound, would be a panel that either never appears or runs off the bottom
// of the screen, and neither shows up in a diff.

import QtQuick
import QtTest
import Phosphor.Bar
import Phosphor.Theme

TestCase {
    id: testCase

    name: "Panel"
    when: windowShown

    Component {
        id: frameComponent

        PanelFrame {
            title: "Network"
            panelWidth: 300
            maxBodyHeight: 100
        }
    }

    // A frame whose body is taller than the cap, so the scroll path is the
    // one under test rather than the plain-growth path.
    Component {
        id: tallFrameComponent

        PanelFrame {
            title: "Long"
            panelWidth: 300
            maxBodyHeight: 100

            Rectangle {
                width: parent.width
                implicitHeight: 400
                height: implicitHeight
            }
        }
    }

    Component {
        id: rowComponent

        PanelRow {
            width: 300
            label: "Home network"
            sublabel: "Secured"
        }
    }

    function test_frameTakesItsDeclaredWidth() {
        const frame = createTemporaryObject(frameComponent, testCase);
        verify(frame);
        // The width is fixed rather than content-derived so a person moving
        // between panels is not re-reading a differently shaped card.
        compare(frame.implicitWidth, 300);
    }

    function test_emptyFrameStillHasAHeight() {
        const frame = createTemporaryObject(frameComponent, testCase);
        verify(frame);
        // The header alone gives the frame a height. A zero here would mean
        // the popout measured the content as nothing and showed an empty
        // card, which is how a panel silently fails to appear.
        verify(frame.implicitHeight > 0);
    }

    function test_aTallBodyScrollsRatherThanGrowing() {
        const frame = createTemporaryObject(tallFrameComponent, testCase);
        verify(frame);
        // 400px of content against a 100px cap. The frame must stay near
        // header-plus-cap rather than growing to fit: an uncapped panel runs
        // off the bottom of the output, where its own scrollbar cannot be
        // reached either.
        verify(frame.implicitHeight < 400);
    }

    function test_rowIsAComfortablePointerTarget() {
        const row = createTemporaryObject(rowComponent, testCase);
        verify(row);
        // The floor, not the text's natural height: a row that shrank to its
        // label would be a 14px click target in a list.
        verify(row.implicitHeight >= 36);
    }

    function test_anUnpressableRowEmitsNothing() {
        const row = createTemporaryObject(rowComponent, testCase);
        verify(row);
        row.pressable = false;

        const spy = createTemporaryObject(signalSpyComponent, testCase, {
            "target": row
        });
        verify(spy);

        // Through the accessible path rather than a synthesised click: the
        // guard lives in the shared activation function, and both the
        // pointer and assistive tech route through it. A row whose service
        // has gone (no device, no player) must not act on a press.
        row.Accessible.pressAction();
        compare(spy.count, 0);

        row.pressable = true;
        row.Accessible.pressAction();
        compare(spy.count, 1);
    }

    Component {
        id: signalSpyComponent

        SignalSpy {
            signalName: "clicked"
        }
    }
}
