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
    // Shell chrome primitives (05 §6). Enter fast, leave slow: enter for a
    // stroke or opacity, enter_content for the body behind it, release
    // for small things, release_long for geometry and retiring map cells,
    // reveal / dismiss for a surface opening and closing, tick for a
    // discrete step (volume notch, clock minute). The M3 durations above
    // stay for settings pages.
    readonly property int duration_enter: 90
    readonly property int duration_enter_content: 140
    readonly property int duration_release: 360
    readonly property int duration_release_long: 720
    readonly property int duration_reveal: 220
    readonly property int duration_dismiss: 140
    readonly property int duration_tick: 60
    // Reduced-motion switch. The shell host sets this from the session's
    // accessibility preference at startup; chrome reads it to drop the
    // idle gleam and the overshoot on enter. Default off.
    property bool reducedMotion: false
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
    // Shell chrome curves, mirroring the data/curves/ files of the same
    // shape (osd-pop, phosphor-release, widget-out, osd-in, widget-pop).
    // Qt's BezierSpline accepts y values outside 0..1, so the overshoot
    // curves are carried verbatim here.
    readonly property list<real> easing_enter: [0.34, 1.20, 0.64, 1.00, 1, 1]
    readonly property list<real> easing_release: [0.05, 0.60, 0.15, 1.00, 1, 1]
    readonly property list<real> easing_reveal: [0.33, 1.00, 0.68, 1.00, 1, 1]
    readonly property list<real> easing_dismiss: [0.32, 0, 0.67, 0, 1, 1]
    readonly property list<real> easing_tick: [0.34, 1.56, 0.64, 1.00, 1, 1]
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
    // Chrome primitives. `settle` is a spring (data/curves/phosphor-settle)
    // and has no bezier form; positional settles go through
    // PhosphorProfile rather than a Behavior easing.
    readonly property var enter: ({
            "type": Easing.BezierSpline,
            "bezierCurve": easing_enter
        })
    readonly property var release: ({
            "type": Easing.BezierSpline,
            "bezierCurve": easing_release
        })
    readonly property var reveal: ({
            "type": Easing.BezierSpline,
            "bezierCurve": easing_reveal
        })
    readonly property var dismiss: ({
            "type": Easing.BezierSpline,
            "bezierCurve": easing_dismiss
        })
    readonly property var tick: ({
            "type": Easing.BezierSpline,
            "bezierCurve": easing_tick
        })
}
