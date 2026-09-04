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
        const spy = createTemporaryObject(signalSpyComp, testCase, {
            "target": b,
            "signalName": "clicked"
        });
        verify(spy.valid, "clicked signal exists");
        b.forceActiveFocus();
        verify(b.activeFocus, "button takes keyboard focus");
        keyClick(Qt.Key_Space);
        compare(spy.count, 1, "clicked actually fires on activation");
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
        compare(r.radius, 0, "square by default");
        compare(r.rippling, false, "no sweep at rest");
    }

    function test_ripple_sweep_lifecycle() {
        const r = createTemporaryObject(rippleComp, testCase, {
            "width": 120,
            "height": 40,
            "radius": 20
        });
        verify(r, "PhosphorRipple instantiates");
        r.start(10, 20);
        compare(r.rippling, true, "start() begins the sweep");
        // The sweep clears on its own; a second press restarts it from
        // the new point rather than stacking on the in-flight one.
        r.start(110, 20);
        compare(r.rippling, true, "a second press restarts the sweep");
        tryVerify(function () {
            return !r.rippling;
        }, 3000, "the sweep finishes");
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

    function test_button_space_and_enter_activate() {
        const b = createTemporaryObject(buttonComp, testCase, {
            "text": "Go"
        });
        const spy = createTemporaryObject(signalSpyComp, testCase, {
            "target": b,
            "signalName": "clicked"
        });
        b.forceActiveFocus();
        verify(b.activeFocus, "button takes keyboard focus");
        keyClick(Qt.Key_Space);
        compare(spy.count, 1, "Space activates the button");
        keyClick(Qt.Key_Return);
        compare(spy.count, 2, "Return activates the button");
    }

    function test_disabled_button_skips_tab_order() {
        const b = createTemporaryObject(buttonComp, testCase, {
            "enabled": false
        });
        compare(b.activeFocusOnTab, false, "a disabled button is not Tab-focusable");
    }

    function test_pill_space_toggles() {
        const p = createTemporaryObject(pillComp, testCase, {
            "text": "Wi-Fi"
        });
        const clickSpy = createTemporaryObject(signalSpyComp, testCase, {
            "target": p,
            "signalName": "clicked"
        });
        const toggleSpy = createTemporaryObject(signalSpyComp, testCase, {
            "target": p,
            "signalName": "toggled"
        });
        p.forceActiveFocus();
        verify(p.activeFocus, "pill takes keyboard focus");
        keyClick(Qt.Key_Space);
        compare(clickSpy.count, 1, "Space emits clicked");
        compare(toggleSpy.count, 1, "Space emits toggled");
    }

    // The keys REQUEST a value; they do not set one. The slider does not
    // assign its own `value`, because a host binds that to the service's echo
    // and a JS assignment here would sever the binding on the first key
    // press, after which the handle would stop following the service. So the
    // assertions are on `moved`'s payload, and on `value` staying put until
    // something answers.
    function test_slider_arrow_keys_request_a_new_value() {
        const s = createTemporaryObject(sliderComp, testCase, {
            "from": 0,
            "to": 100,
            "value": 50,
            "width": 200
        });
        const spy = createTemporaryObject(signalSpyComp, testCase, {
            "target": s,
            "signalName": "moved"
        });
        s.forceActiveFocus();
        verify(s.activeFocus, "slider takes keyboard focus");
        compare(s._step, 5, "default step is a twentieth of the range");

        keyClick(Qt.Key_Right);
        compare(spy.count, 1, "moved emitted on the arrow nudge");
        compare(spy.signalArguments[0][0], 55, "Right arrow requests one step up");
        compare(s.value, 50, "value is unchanged until the host answers");

        // The host answers, the way a service echo would.
        s.value = 55;
        keyClick(Qt.Key_Left);
        compare(spy.signalArguments[1][0], 50, "Left arrow requests one step down");
        keyClick(Qt.Key_Home);
        compare(spy.signalArguments[2][0], 0, "Home requests from");
        keyClick(Qt.Key_End);
        compare(spy.signalArguments[3][0], 100, "End requests to");
    }

    // The binding this component exists to protect. A host binds `value` to a
    // service echo; after any interaction that binding must still be live, or
    // a change made elsewhere stops moving the handle.
    function test_slider_value_binding_survives_interaction() {
        const holder = createTemporaryObject(sliderBindingHolderComp, testCase);
        const s = holder.slider;
        s.forceActiveFocus();

        compare(s.value, 30, "starts at the bound value");
        keyClick(Qt.Key_Right);
        // The service moves on its own, as it would after a media key or
        // another client's change.
        holder.serviceValue = 80;
        compare(s.value, 80, "the handle still follows the service after a key press");
    }

    Component {
        id: signalSpyComp

        SignalSpy {}
    }

    // A host in miniature: a slider whose `value` is BOUND to a separate
    // property standing in for the service's echo.
    Component {
        id: sliderBindingHolderComp

        Item {
            property real serviceValue: 30
            property alias slider: inner

            PhosphorSlider {
                id: inner

                from: 0
                to: 100
                width: 200
                value: parent.serviceValue
            }
        }
    }
}
