// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Theme.Motion, duration and easing tokens (Material 3 motion).
// Wire NumberAnimation and Behavior elements via these instead of
// one-off durations and Easing.* picks. A single tuning here propagates
// across every animated surface.

pragma Singleton

import QtQuick

QtObject {
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
    // ─── Easings ─────────────────────────────────────────────────────────
    // M3 standard, emphasized, decelerated, and accelerated curves.
    // Stored as bezier control-point arrays in Qt's BezierSpline format.
    // Each array is the four-control-point cubic-bezier from M3, padded
    // with the curve endpoint at 1,1 as Qt's BezierSpline requires.
    // Typed list<real> so QML stores each control point as a double in
    // C++. Plain `property var` would round-trip through JS Number, and
    // V4 tags integer-valued Numbers as Int, producing QVariant(int)
    // elements that Easing.bezierCurve (QList<double>) refuses with a
    // runtime "Could not convert ... to QList<double>" log.
    readonly property list<real> easing_standard: [0.2, 0, 0, 1, 1, 1]
    readonly property list<real> easing_emphasized: [0.05, 0.7, 0.1, 1, 1, 1]
    readonly property list<real> easing_decelerated: [0, 0, 0.2, 1, 1, 1]
    readonly property list<real> easing_accelerated: [0.3, 0, 1, 1, 1, 1]
    // Pre-built easing objects. Hand directly to Behavior.easing. Example.
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
