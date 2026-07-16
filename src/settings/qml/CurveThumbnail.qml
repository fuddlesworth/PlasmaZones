// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "EasingCurve.js" as Easing
import QtQuick
import QtQuick.Controls
import "SpringPhysics.js" as Spring
import org.kde.kirigami as Kirigami

/**
 * @brief Small read-only curve preview (~80x50px) for inline display in event cards.
 *
 * Renders a bezier curve or spring oscillation waveform. Clickable to open the
 * full CurveEditorDialog.
 *
 * Spring inputs are (omega, zeta) per `PhosphorAnimation::Spring`. The shared
 * SpringPhysics.js takes (stiffness, dampingRatio) where stiffness == ω², so
 * the call sites pass `omega * omega` for stiffness. A small fixed
 * `_settleEpsilon` is used internally for settle-time estimation — the
 * thumbnail doesn't expose epsilon as a knob.
 */
Rectangle {
    id: root

    required property string curve
    required property int timingMode
    property real omega: 12 ///< Snappy default (rad/s)
    property real zeta: 1 ///< Critically damped default
    // Settle threshold for visual settle-time estimation. Not user-tunable
    // here — the surrounding UI sets duration explicitly when it matters.
    readonly property real _settleEpsilon: 0.005

    signal clicked

    implicitWidth: Kirigami.Units.gridUnit * 5
    implicitHeight: Kirigami.Units.gridUnit * 3
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.backgroundColor
    border.color: (hoverArea.containsMouse || root.activeFocus) ? Kirigami.Theme.highlightColor : (Kirigami.Theme.separatorColor !== undefined ? Kirigami.Theme.separatorColor : Kirigami.Theme.disabledTextColor)
    border.width: root.activeFocus ? 2 : 1
    Accessible.name: i18n("Curve preview")
    Accessible.role: Accessible.Button
    Accessible.focusable: true
    // Keyboard and focus support, matching ColorButton.
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    // Numpad Enter alias, matching the sibling card components.
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    // Repaint when inputs change or when component is ready (theme available)
    Component.onCompleted: canvas.repaintCurve()
    onCurveChanged: canvas.repaintCurve()
    onTimingModeChanged: canvas.repaintCurve()
    onOmegaChanged: canvas.repaintCurve()
    onZetaChanged: canvas.repaintCurve()

    Canvas {
        id: canvas

        // Cache resolved color string for Canvas 2D context
        property string _strokeColor: ""

        function repaintCurve() {
            _strokeColor = Kirigami.Theme.highlightColor.toString();
            requestPaint();
        }

        function evaluateEasing(t, curveStr) {
            return Easing.evaluate(t, curveStr);
        }

        anchors.fill: parent
        anchors.margins: Kirigami.Units.smallSpacing
        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            if (!_strokeColor)
                return;

            var w = width;
            var h = height;
            var pad = Math.round(Kirigami.Units.smallSpacing / 2);
            var drawW = w - 2 * pad;
            var drawH = h - 2 * pad;
            if (root.timingMode === CurvePresets.timingModeEasing) {
                // Sample to find Y range (elastic/bounce overshoot)
                var steps = 60;
                var yMin = 0, yMax = 1;
                var samples = [];
                for (var s = 0; s <= steps; s++) {
                    var sv = evaluateEasing(s / steps, root.curve);
                    samples.push(sv);
                    if (sv < yMin)
                        yMin = sv;

                    if (sv > yMax)
                        yMax = sv;
                }
                // Add padding to Y range
                var yRange = Math.max(yMax - yMin, 0.01);
                yMin -= yRange * 0.08;
                yMax += yRange * 0.08;
                yRange = yMax - yMin;
                ctx.strokeStyle = _strokeColor;
                ctx.lineWidth = 1;
                ctx.beginPath();
                for (var i = 0; i <= steps; i++) {
                    var px = pad + (i / steps) * drawW;
                    var py = pad + drawH - ((samples[i] - yMin) / yRange) * drawH;
                    if (i === 0)
                        ctx.moveTo(px, py);
                    else
                        ctx.lineTo(px, py);
                }
                ctx.stroke();
            } else {
                // Spring: same physics as SpringPreview / C++ Spring
                // (damped harmonic oscillator). SpringPhysics.js's
                // `stiffness` arg is ω² with unit mass; pass it as such.
                var spSteps = 60;
                var spMin = 0, spMax = 1;
                var spSamples = [];
                var stiffness = root.omega * root.omega;
                var settleTime = Spring.estimateSettleTime(stiffness, root.zeta, root._settleEpsilon);
                var maxTime = Math.max(settleTime * 1.3, 0.3);
                for (var si = 0; si <= spSteps; si++) {
                    var t = (si / spSteps) * maxTime;
                    var sy = Spring.evaluate(t, stiffness, root.zeta, 0);
                    spSamples.push(sy);
                    if (sy < spMin)
                        spMin = sy;

                    if (sy > spMax)
                        spMax = sy;
                }
                var spRange = Math.max(spMax - spMin, 0.01);
                spMin -= spRange * 0.08;
                spMax += spRange * 0.08;
                spRange = spMax - spMin;
                ctx.strokeStyle = _strokeColor;
                ctx.lineWidth = 1;
                ctx.beginPath();
                for (var sj = 0; sj <= spSteps; sj++) {
                    var spx = pad + (sj / spSteps) * drawW;
                    var spy = pad + drawH - ((spSamples[sj] - spMin) / spRange) * drawH;
                    if (sj === 0)
                        ctx.moveTo(spx, spy);
                    else
                        ctx.lineTo(spx, spy);
                }
                ctx.stroke();
            }
        }

        // Re-resolve color when Kirigami theme actually becomes available
        Connections {
            function onHighlightColorChanged() {
                canvas.repaintCurve();
            }

            target: Kirigami.Theme
        }
    }

    MouseArea {
        id: hoverArea

        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            // Move active focus here on click so a previously keyboard-focused
            // sibling doesn't keep the focus ring and the key handlers.
            root.forceActiveFocus();
            root.clicked();
        }
    }
}
