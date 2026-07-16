// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "SpringPhysics.js" as Spring
import org.kde.kirigami as Kirigami

/**
 * @brief Spring physics visualization with damped oscillation graph and animated preview box.
 *
 * Matches EasingPreview's dimensions and visual weight (240px graph + 36px box track).
 *
 * Required properties:
 *   - omega: real (rad/s, [0.1, 200] — matches PhosphorAnimation::Spring)
 *   - zeta:  real (damping ratio, [0.0, 10.0])
 *   - previewEnabled: bool
 *
 * SpringPhysics.js takes (stiffness, dampingRatio); stiffness == ω² with unit
 * mass, so call sites pass `omega * omega`. Settle-time epsilon is fixed for
 * visual purposes — surrounding UIs set duration explicitly when it matters.
 */
Item {
    id: root

    required property real omega
    required property real zeta
    required property bool previewEnabled

    onVisibleChanged: if (!visible) {
        springAnimTimer.stop();
        springReplayDelay.stop();
    }
    // Match EasingPreview dimensions exactly
    readonly property int canvasHeight: Kirigami.Units.gridUnit * 15
    readonly property int boxTrackHeight: Kirigami.Units.gridUnit * 2
    readonly property int boxSize: Math.round(Kirigami.Units.gridUnit * 1.5)
    readonly property int canvasPad: Math.round(Kirigami.Units.gridUnit * 1.5)
    // Settle threshold for the visual settle-time estimate (controls how much
    // of the curve we draw). Not user-tunable — see class doc.
    readonly property real _settleEpsilon: 0.005
    // Replay/animation cadence — hoisted so the magic numbers live with
    // their semantic name instead of inline on the Timer bindings below.
    readonly property int _replayDelayMs: 80
    readonly property int _animTickMs: 16 // ~60fps
    // Y-axis range for the oscillation graph: [-yOffset, yRange-yOffset].
    // Reserves headroom above 1.0 / below 0.0 for underdamped overshoot
    // so the curve and reference lines remain visible.
    readonly property real _yRange: 1.4
    readonly property real _yOffset: 0.1

    function evaluateSpring(t) {
        return Spring.evaluate(t, root.omega * root.omega, root.zeta, 0);
    }

    function estimateSettleTime() {
        return Spring.estimateSettleTime(root.omega * root.omega, root.zeta, root._settleEpsilon);
    }

    function replay() {
        if (!root.previewEnabled)
            return;

        springAnimTimer.stop();
        boxTrack.animBox.x = 0;
        springCanvas.requestPaint();
        springReplayDelay.restart();
    }

    // Spring description label
    function springLabel() {
        var z = root.zeta;
        var label = "";
        if (z < 0.7)
            label = i18n("bouncy");
        else if (z < 0.95)
            label = i18n("snappy");
        else if (Math.abs(z - 1) < 0.05)
            label = i18n("critical");
        else
            label = i18n("overdamped");
        var settleMs = Math.round(estimateSettleTime() * 1000);
        return i18n("spring(%1, ~%2ms)", label, settleMs);
    }

    implicitWidth: Kirigami.Units.gridUnit * 25
    implicitHeight: layout.implicitHeight
    Accessible.name: i18n("Spring animation preview")
    Accessible.role: Accessible.Graphic
    // Auto-replay on parameter changes
    onOmegaChanged: replay()
    onZetaChanged: replay()

    ColumnLayout {
        id: layout

        anchors.fill: parent
        spacing: 0

        // ── Oscillation graph (same height as EasingPreview canvas) ───
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.canvasHeight
            radius: Kirigami.Units.smallSpacing * 2
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.alternateBackgroundColor

            Canvas {
                id: springCanvas

                function yToPixel(yVal, pad, gh, yOffset, yRange) {
                    return pad + gh - ((yVal + yOffset) / yRange) * gh;
                }

                anchors.fill: parent
                Component.onCompleted: requestPaint()
                onPaint: {
                    // Envelope for underdamped (zeta < 1).

                    var ctx = getContext("2d");
                    ctx.clearRect(0, 0, width, height);
                    // Resolve colors fresh each paint so theme context is always current
                    var accentStr = Kirigami.Theme.highlightColor.toString();
                    var accentDimStr = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.2).toString();
                    var gridStr = Kirigami.ColorUtils.linearInterpolation(Kirigami.Theme.backgroundColor, Kirigami.Theme.textColor, Kirigami.Theme.frameContrast).toString();
                    var w = width;
                    var h = height;
                    var pad = root.canvasPad;
                    var gw = w - 2 * pad;
                    var gh = h - 2 * pad;
                    // Y mapping: 0.0 at bottom, 1.0 near top (leave room for overshoot)
                    var yRange = root._yRange;
                    // show -0.1 to 1.3
                    var yOffset = root._yOffset;
                    // Grid lines
                    ctx.strokeStyle = gridStr;
                    ctx.lineWidth = 0.5;
                    for (var gy = 0; gy <= 1; gy += 0.25) {
                        var py = yToPixel(gy, pad, gh, yOffset, yRange);
                        ctx.beginPath();
                        ctx.moveTo(pad, py);
                        ctx.lineTo(w - pad, py);
                        ctx.stroke();
                    }
                    // Vertical grid
                    for (var gx = 0; gx <= 1; gx += 0.2) {
                        var px = pad + gx * gw;
                        ctx.beginPath();
                        ctx.moveTo(px, pad);
                        ctx.lineTo(px, pad + gh);
                        ctx.stroke();
                    }
                    // Reference lines at y=0 and y=1 (dashed)
                    var targetPy = yToPixel(1, pad, gh, yOffset, yRange);
                    var zeroPy = yToPixel(0, pad, gh, yOffset, yRange);
                    ctx.strokeStyle = Qt.alpha(Kirigami.Theme.highlightColor, 0.6).toString();
                    ctx.lineWidth = 1;
                    ctx.setLineDash([6, 4]);
                    ctx.beginPath();
                    ctx.moveTo(pad, targetPy);
                    ctx.lineTo(w - pad, targetPy);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(pad, zeroPy);
                    ctx.lineTo(w - pad, zeroPy);
                    ctx.stroke();
                    ctx.setLineDash([]);
                    // Spring curve
                    var settleTime = root.estimateSettleTime();
                    var maxTime = Math.max(settleTime * 1.3, 0.3);
                    var steps = 300;
                    ctx.strokeStyle = accentStr;
                    ctx.lineWidth = 2;
                    ctx.beginPath();
                    for (var i = 0; i <= steps; i++) {
                        var t = (i / steps) * maxTime;
                        var y = root.evaluateSpring(t);
                        var cpx = pad + (i / steps) * gw;
                        var cpy = yToPixel(y, pad, gh, yOffset, yRange);
                        if (i === 0)
                            ctx.moveTo(cpx, cpy);
                        else
                            ctx.lineTo(cpx, cpy);
                    }
                    ctx.stroke();
                    // The true step response is
                    //   y(t) = 1 − e^(−ζω t)·(cos(ω_d t) + (ζ/√(1−ζ²))·sin(ω_d t))
                    // whose oscillation extrema reach 1 ± e^(−ζω t)/√(1−ζ²),
                    // NOT 1 ± e^(−ζω t). The simpler-looking envelope
                    // underestimates the bound by a factor of 1/√(1−ζ²) and
                    // the rendered curve visibly pokes outside the dashed
                    // lines at low ζ — defeating the lines' purpose.
                    if (root.zeta < 1) {
                        ctx.strokeStyle = accentDimStr;
                        ctx.lineWidth = 0.5;
                        ctx.setLineDash([3, 3]);
                        const envScale = 1 / Math.sqrt(1 - root.zeta * root.zeta);
                        // Upper envelope
                        ctx.beginPath();
                        for (var j = 0; j <= steps; j++) {
                            var te = (j / steps) * maxTime;
                            var env = Math.exp(-root.zeta * root.omega * te) * envScale;
                            var pxe = pad + (j / steps) * gw;
                            var pye = yToPixel(1 + env, pad, gh, yOffset, yRange);
                            if (j === 0)
                                ctx.moveTo(pxe, pye);
                            else
                                ctx.lineTo(pxe, pye);
                        }
                        ctx.stroke();
                        // Lower envelope
                        ctx.beginPath();
                        for (var k = 0; k <= steps; k++) {
                            var te2 = (k / steps) * maxTime;
                            var env2 = Math.exp(-root.zeta * root.omega * te2) * envScale;
                            var pxe2 = pad + (k / steps) * gw;
                            var pye2 = yToPixel(1 - env2, pad, gh, yOffset, yRange);
                            if (k === 0)
                                ctx.moveTo(pxe2, pye2);
                            else
                                ctx.lineTo(pxe2, pye2);
                        }
                        ctx.stroke();
                        ctx.setLineDash([]);
                    }
                    // Axis labels
                    ctx.fillStyle = Kirigami.Theme.disabledTextColor.toString();
                    ctx.font = Kirigami.Theme.smallFont.pointSize + "pt sans-serif";
                    ctx.textAlign = "right";
                    ctx.fillText("1", pad - 4, targetPy + 4);
                    ctx.fillText("0", pad - 4, yToPixel(0, pad, gh, yOffset, yRange) + 4);
                }
            }

            // onPaint samples highlight, text, background and positive-text
            // colours. Every PlatformTheme colour shares the one `colorsChanged`
            // notify signal, so this one handler covers all of them.
            Connections {
                function onColorsChanged() {
                    springCanvas.requestPaint();
                }

                target: Kirigami.Theme
            }

            // Click to replay
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                Accessible.name: i18n("Click to replay spring animation")
                Accessible.role: Accessible.Button
                onClicked: root.replay()
            }
        }

        // ── Spring description label ──────────────────────────────────
        Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.smallSpacing
            Layout.bottomMargin: Kirigami.Units.smallSpacing
            horizontalAlignment: Text.AlignHCenter
            text: root.springLabel()
            color: Kirigami.Theme.disabledTextColor
            font: Kirigami.Theme.fixedWidthFont
        }

        // ── Animated box track (matches EasingPreview exactly) ────────
        AnimatedBoxTrack {
            id: boxTrack

            Layout.fillWidth: true
            Layout.preferredHeight: root.boxTrackHeight
            boxSize: root.boxSize
        }
    }

    // Replay delay (matches EasingPreview pattern)
    Timer {
        id: springReplayDelay

        interval: root._replayDelayMs
        onTriggered: {
            springAnimTimer.elapsed = 0;
            springAnimTimer.lastTime = Date.now();
            springAnimTimer.settleMs = root.estimateSettleTime() * 1000;
            springAnimTimer.start();
        }
    }

    // Animation tick timer (~60fps)
    Timer {
        id: springAnimTimer

        property real elapsed: 0
        property real lastTime: 0
        property real settleMs: 2000

        interval: root._animTickMs
        repeat: true
        onTriggered: {
            var now = Date.now();
            elapsed += (now - lastTime);
            lastTime = now;
            var tSeconds = elapsed / 1000;
            var springPos = root.evaluateSpring(tSeconds);
            var trackWidth = Math.max(0, boxTrack.animBox.parent.width - root.boxSize);
            // Allow visual overshoot for underdamped springs (can go past 1.0)
            var maxOvershoot = trackWidth * 1.15;
            boxTrack.animBox.x = Math.max(-trackWidth * 0.05, Math.min(maxOvershoot, springPos * trackWidth));
            if (elapsed >= settleMs) {
                boxTrack.animBox.x = trackWidth;
                springAnimTimer.stop();
            }
        }
    }
}
