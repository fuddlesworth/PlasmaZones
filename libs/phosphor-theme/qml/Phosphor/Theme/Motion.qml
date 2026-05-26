// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Motion — duration and easing tokens (Material 3 motion).
// Wire NumberAnimation / Behavior {} via these instead of one-off
// durations and Easing.* picks: a single tuning here propagates across
// every animated surface.

import QtQuick
pragma Singleton

QtObject {
    // ─── Easings ─────────────────────────────────────────────────────────
    // M3 "standard" / "emphasized" / "decelerated" / "accelerated" curves.
    // Stored as bezier control-point arrays so consumers can hand them to
    // Easing { type: Easing.BezierSpline; bezierCurve: ... }.

    // ─── Durations (ms) ──────────────────────────────────────────────────
    // M3 motion duration tokens.
    readonly property int duration_short_1: 50
    readonly property int duration_short_2: 100
    readonly property int duration_short_3: 150
    readonly property int duration_short_4: 200
    readonly property int duration_medium_1: 250
    readonly property int duration_medium_2: 300
    readonly property int duration_medium_3: 350
    readonly property int duration_medium_4: 400
    readonly property int duration_long_1: 450
    readonly property int duration_long_2: 500
    readonly property int duration_long_3: 550
    readonly property int duration_long_4: 600
    readonly property int duration_extra_long_1: 700
    readonly property int duration_extra_long_2: 800
    readonly property int duration_extra_long_3: 900
    readonly property int duration_extra_long_4: 1000
    // Pass each one as the four-control-point cubic-bezier from M3:
    //   [ x1, y1, x2, y2, 1, 1 ] — final pair pins the curve at (1,1).
    readonly property var easing_standard: [0.2, 0, 0, 1, 1, 1]
    readonly property var easing_emphasized: [0.05, 0.7, 0.1, 1, 1, 1]
    readonly property var easing_decelerated: [0, 0, 0.2, 1, 1, 1]
    readonly property var easing_accelerated: [0.3, 0, 1, 1, 1, 1]
    // Helpers — pre-built Easing{} factories. Use as
    //   Behavior on x { NumberAnimation { duration: Motion.duration_medium_2; easing: Motion.standard } }
    readonly property var standard: ({
        "type": Easing.BezierSpline,
        "bezierCurve": easing_standard
    })
    readonly property var emphasized: ({
        "type": Easing.BezierSpline,
        "bezierCurve": easing_emphasized
    })
    readonly property var decelerated: ({
        "type": Easing.BezierSpline,
        "bezierCurve": easing_decelerated
    })
    readonly property var accelerated: ({
        "type": Easing.BezierSpline,
        "bezierCurve": easing_accelerated
    })
}
