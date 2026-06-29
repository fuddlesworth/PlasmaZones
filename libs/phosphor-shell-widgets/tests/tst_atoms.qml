// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Smoke + contract tests for the Phosphor.Widgets atoms. These pin each
// atom's default property surface and the small amount of pure logic the
// atoms carry (slider ratio clamping, elevation level clamping), so a
// renamed property or a broken default fails the build rather than
// surfacing as a silent UI bug. Visual behaviour (hover tint, ripple
// sweep, retinting) is exercised by the kitchen-sink demo, not here.

import QtQuick
import QtTest
import Phosphor.Widgets

TestCase {
    id: testCase

    name: "PhosphorAtoms"

    Component {
        id: buttonComp

        PhosphorButton {}
    }

    Component {
        id: pillComp

        PhosphorPill {}
    }

    Component {
        id: cardComp

        PhosphorCard {}
    }

    Component {
        id: sliderComp

        PhosphorSlider {}
    }

    Component {
        id: textFieldComp

        PhosphorTextField {}
    }

    Component {
        id: rippleComp

        PhosphorRipple {}
    }

    Component {
        id: shadowComp

        ElevationShadow {}
    }

    function test_button_defaults() {
        const b = createTemporaryObject(buttonComp, testCase);
        verify(b, "PhosphorButton instantiates");
        compare(b.text, "", "default text empty");
        compare(b.variant, PhosphorButton.Filled, "default variant Filled");
        verify(b.implicitHeight > 0, "has an implicit height");
    }

    function test_button_clicked_signal() {
        const b = createTemporaryObject(buttonComp, testCase);
        const spy = signalSpyComp.createObject(testCase, {
            "target": b,
            "signalName": "clicked"
        });
        verify(spy.valid, "clicked signal exists");
    }

    function test_pill_defaults() {
        const p = createTemporaryObject(pillComp, testCase);
        verify(p, "PhosphorPill instantiates");
        compare(p.text, "", "default text empty");
        compare(p.selected, false, "default unselected");
    }

    function test_card_defaults() {
        const c = createTemporaryObject(cardComp, testCase);
        verify(c, "PhosphorCard instantiates");
        compare(c.elevation, 1, "default elevation 1");
        compare(c.radius, 16, "default radius 16");
        compare(c.padding, 16, "default padding 16");
    }

    function test_slider_ratio_clamps() {
        const s = createTemporaryObject(sliderComp, testCase, {
            "from": 0,
            "to": 10,
            "width": 120
        });
        verify(s, "PhosphorSlider instantiates");
        s.value = 5;
        compare(s._ratio, 0.5, "midpoint maps to 0.5");
        s.value = 20;
        compare(s._ratio, 1, "over-range value clamps the ratio to 1");
        s.value = -5;
        compare(s._ratio, 0, "under-range value clamps the ratio to 0");
    }

    function test_slider_zero_range_is_safe() {
        const s = createTemporaryObject(sliderComp, testCase, {
            "from": 5,
            "to": 5
        });
        compare(s._ratio, 0, "from == to collapses ratio to 0, not NaN");
    }

    function test_textfield_text_alias() {
        const t = createTemporaryObject(textFieldComp, testCase);
        verify(t, "PhosphorTextField instantiates");
        compare(t.text, "", "default text empty");
        t.text = "hello";
        compare(t.text, "hello", "text alias round-trips");
    }

    function test_ripple_defaults() {
        const r = createTemporaryObject(rippleComp, testCase);
        verify(r, "PhosphorRipple instantiates");
        compare(r.interactive, true, "interactive by default");
        compare(r.down, false, "not pressed at rest");
        compare(r.hovered, false, "not hovered at rest");
    }

    function test_shadow_level_clamps() {
        const e = createTemporaryObject(shadowComp, testCase, {
            "level": 9
        });
        verify(e, "ElevationShadow instantiates");
        compare(e._level, 5, "over-range level clamps to 5");
        e.level = -3;
        compare(e._level, 0, "under-range level clamps to 0");
        compare(e.shadowEnabled, false, "level 0 disables the shadow");
    }

    Component {
        id: signalSpyComp

        SignalSpy {}
    }
}
