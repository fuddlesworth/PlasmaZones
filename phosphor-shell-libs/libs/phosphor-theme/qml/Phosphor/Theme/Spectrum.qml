// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Shared structure and state ramp from the active appearance.

pragma Singleton

import QtQuick

QtObject {
    id: spectrum

    readonly property var stops: Appearance.stops

    // State-axis names for the four stops: cyan resting/info, blue
    // active/selected, purple pending/transient, rose hot/destructive.
    readonly property color resting: stops[0]
    readonly property color active: stops[1]
    readonly property color pending: stops[2]
    readonly property color hot: stops[3]
    // Focus and urgency are white, never a hue (R9).
    readonly property color focus: Appearance.light ? Appearance.text : "#FFFFFF"

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
}
