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
 */
Rectangle {
    id: root

    required property string curve
    required property int timingMode
    property real springDamping: 1
    property real springStiffness: 800
    property real springEpsilon: 0.0001

    signal clicked()

    implicitWidth: Kirigami.Units.gridUnit * 5
    implicitHeight: Kirigami.Units.gridUnit * 3
    radius: Kirigami.Units.smallSpacing
    color: Kirigami.Theme.backgroundColor
    border.color: hoverArea.containsMouse ? Kirigami.Theme.highlightColor : (Kirigami.Theme.separatorColor ?? Kirigami.Theme.disabledTextColor)
    border.width: 1
    Accessible.name: i18n("Curve preview")
    Accessible.role: Accessible.Button
    // Repaint when inputs change or when component is ready (theme available)
    Component.onCompleted: canvas.repaintCurve()
    onCurveChanged: canvas.repaintCurve()
    onTimingModeChanged: canvas.repaintCurve()
    onSpringDampingChanged: canvas.repaintCurve()
    onSpringStiffnessChanged: canvas.repaintCurve()
    onSpringEpsilonChanged: canvas.repaintCurve()

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
                return ;

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
                ctx.lineWidth = Math.max(1, Math.round(Kirigami.Units.devicePixelRatio));
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
                // Spring: use same physics as SpringPreview/C++ (damped harmonic oscillator)
                var spSteps = 60;
                var spMin = 0, spMax = 1;
                var spSamples = [];
                var damping = root.springDamping;
                var settleTime = Spring.estimateSettleTime(root.springStiffness, damping, root.springEpsilon);
                var maxTime = Math.max(settleTime * 1.3, 0.3);
                for (var si = 0; si <= spSteps; si++) {
                    var t = (si / spSteps) * maxTime;
                    var sy = Spring.evaluate(t, root.springStiffness, damping, 0);
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
                ctx.lineWidth = Math.max(1, Math.round(Kirigami.Units.devicePixelRatio));
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
        onClicked: root.clicked()
    }

}
