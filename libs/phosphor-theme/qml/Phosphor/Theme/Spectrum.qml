// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Spectrum, the brand ramp as a sampler.
//
// The whole shell shares one cyan → blue → purple → rose axis and every
// coloured element is a coordinate on it (05-visual-identity.md §4.1):
// `at(t)` samples the ramp for a rail position, a strip position or a
// state level. The stops are brand-fixed (R8): they come from the
// palette's brand_stop_* tokens, which matugen never retints, and swap
// to the light ramp when the field is bright. Focus is white (R9) so it
// can never be mistaken for a position.

pragma Singleton

import QtQuick

QtObject {
    id: spectrum

    // Light-field ramp (A1 §2.5). Literals by design: the palette wire
    // format carries a single set of brand stops (the dark ramp), so the
    // light variant has no token to read.
    readonly property list<color> _lightStops: ["#0EA5E9", "#3B82F6", "#7C3AED", "#E11D48"]
    // Reads Theme.brand_stop_* as property reads so a palette reload
    // re-evaluates every `at()` binding that goes through `stops`.
    readonly property list<color> _darkStops: [Theme.brand_stop_0, Theme.brand_stop_1, Theme.brand_stop_2, Theme.brand_stop_3]

    // The four stops in ramp order for the active field polarity.
    readonly property var stops: Theme.isDark ? _darkStops : _lightStops

    // State-axis names for the four stops: cyan resting/info, blue
    // active/selected, purple pending/transient, rose hot/destructive.
    readonly property color resting: stops[0]
    readonly property color active: stops[1]
    readonly property color pending: stops[2]
    readonly property color hot: stops[3]
    // Focus and urgency are white, never a hue (R9).
    readonly property color focus: "#FFFFFF"

    // Piecewise-linear sample of the ramp at t, clamped to 0..1.
    function at(t) {
        const s = stops;
        const n = Number(t);
        const c = Math.max(0, Math.min(1, isNaN(n) ? 0 : n));
        const scaled = c * (s.length - 1);
        const i = Math.min(s.length - 2, Math.floor(scaled));
        const f = scaled - i;
        const a = s[i];
        const b = s[i + 1];
        return Qt.rgba(a.r + (b.r - a.r) * f, a.g + (b.g - a.g) * f, a.b + (b.b - a.b) * f, 1);
    }

    // Rail-axis coordinate for an x position along an edge of `width`.
    function tForX(x, width) {
        if (!(width > 0))
            return 0;
        return Math.max(0, Math.min(1, x / width));
    }

    // State-axis sample: a continuous level (0 cyan, 1 rose).
    function stateColor(level) {
        return at(level);
    }
}
