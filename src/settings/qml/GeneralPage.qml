// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Flickable {
    id: root

    // Layout constants (previously from monolith's QtObject)
    readonly property int sliderPreferredWidth: 200
    readonly property int sliderValueLabelWidth: 40
    // Capture the context property so child components can access it
    readonly property var kcmModule: kcm

    contentHeight: content.implicitHeight
    clip: true
    Component.onCompleted: {
        easingPreview.curve = kcm.animationEasingCurve;
    }

    ColumnLayout {
        id: content

        width: parent.width
        spacing: Kirigami.Units.largeSpacing

        // ═══════════════════════════════════════════════════════════════════════
        // ANIMATIONS CARD
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: animationsCard.implicitHeight

            Kirigami.Card {
                // ═══════════════════════════════════════════════════════════
                // End of inlined EasingPreview
                // ═══════════════════════════════════════════════════════════

                id: animationsCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("Animations")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: ColumnLayout {
                    spacing: Kirigami.Units.largeSpacing

                    // Enable toggle
                    CheckBox {
                        id: animationsEnabledCheck

                        Layout.fillWidth: true
                        text: i18n("Smooth window geometry transitions")
                        checked: kcm.animationsEnabled
                        onToggled: kcm.animationsEnabled = checked
                        ToolTip.visible: hovered
                        ToolTip.text: i18n("Animate windows when snapping to zones or tiling. Applies to both manual snapping and autotiling.")
                    }

                    // ═══════════════════════════════════════════════════════════
                    // EasingPreview (inlined from kcm/ui/EasingPreview.qml)
                    // ═══════════════════════════════════════════════════════════
                    Item {
                        id: easingPreview

                        property string curve: easingPreview.defaultCurve
                        property int animationDuration: kcm.animationDuration
                        property bool previewEnabled: animationsEnabledCheck.checked
                        readonly property int canvasHeight: 240
                        readonly property int boxTrackHeight: 36
                        readonly property int boxSize: 22
                        readonly property int handleRadius: 10
                        readonly property int canvasPad: 24
                        readonly property int handleHitRadius: 18
                        // Y range: [-1, 2] mapped to canvas, allowing overshoot
                        readonly property real yMin: -1
                        readonly property real yMax: 2
                        // Default curve string (Ease Out Cubic) — matches plasmazones.kcfg
                        readonly property string defaultCurve: "0.33,1.00,0.68,1.00"
                        // Parsed control point values (internal state, bezier only)
                        property real cp1x: 0.33
                        property real cp1y: 1
                        property real cp2x: 0.68
                        property real cp2y: 1
                        // Named curve state — set imperatively by parseCurve(), not via binding,
                        // to avoid reading stale _parsedCurveType before parseCurve() runs.
                        property string curveType: "bezier"
                        property real elasticAmplitude: 1
                        property real elasticPeriod: 0.3
                        property int bouncesCount: 3
                        // Known named curve types for validation
                        readonly property var _knownCurveTypes: ["elastic-in", "elastic-out", "elastic-in-out", "bounce-in", "bounce-out", "bounce-in-out"]
                        readonly property bool isBezier: curveType === "bezier"

                        signal curveEdited(string newCurve)

                        // Detect whether a string contains curve-name letters (not just 'e'/'E'
                        // from scientific notation like "1e-1,0.5,0.8,1.0").
                        function _hasLetter(str) {
                            for (var i = 0; i < str.length; i++) {
                                var c = str.charAt(i);
                                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                                    if (c !== 'e' && c !== 'E')
                                        return true;

                                }
                            }
                            return false;
                        }

                        // Parse the curve string into control point properties or named curve state
                        function parseCurve() {
                            var str = easingPreview.curve;
                            if (!str || str === "") {
                                _resetToDefault();
                                return ;
                            }
                            if (_hasLetter(str)) {
                                var colonIdx = str.indexOf(':');
                                var name = colonIdx >= 0 ? str.substring(0, colonIdx).trim() : str.trim();
                                var params = colonIdx >= 0 ? str.substring(colonIdx + 1).trim() : "";
                                // Validate curve name; unknown names fall back to bezier defaults
                                if (_knownCurveTypes.indexOf(name) < 0) {
                                    _resetToDefault();
                                    return ;
                                }
                                var isElastic = (name === "elastic-in" || name === "elastic-out" || name === "elastic-in-out");
                                var isBounce = (name === "bounce-in" || name === "bounce-out" || name === "bounce-in-out");
                                // Reset to defaults before applying params to avoid stale values
                                // from a previous curve type leaking through
                                elasticAmplitude = 1;
                                elasticPeriod = 0.3;
                                bouncesCount = 3;
                                if (params) {
                                    var parts = params.split(",");
                                    if (parts.length >= 1) {
                                        var a = parseFloat(parts[0]);
                                        if (isFinite(a))
                                            elasticAmplitude = Math.max(0.5, Math.min(3, a));

                                    }
                                    if (parts.length >= 2) {
                                        if (isElastic) {
                                            var p = parseFloat(parts[1]);
                                            if (isFinite(p))
                                                elasticPeriod = Math.max(0.1, Math.min(1, p));

                                        } else if (isBounce) {
                                            var b = Math.round(parseFloat(parts[1]));
                                            if (isFinite(b))
                                                bouncesCount = Math.max(1, Math.min(8, b));

                                        }
                                    }
                                }
                                curveType = name;
                                return ;
                            }
                            // Bezier: "x1,y1,x2,y2"
                            curveType = "bezier";
                            var bparts = str.split(",");
                            if (bparts.length === 4) {
                                var x1 = parseFloat(bparts[0]);
                                var y1 = parseFloat(bparts[1]);
                                var x2 = parseFloat(bparts[2]);
                                var y2 = parseFloat(bparts[3]);
                                if (isFinite(x1) && isFinite(y1) && isFinite(x2) && isFinite(y2)) {
                                    // Clamp X to [0,1] and Y to [-1,2] matching C++ fromString bounds
                                    cp1x = Math.max(0, Math.min(1, x1));
                                    cp1y = Math.max(-1, Math.min(2, y1));
                                    cp2x = Math.max(0, Math.min(1, x2));
                                    cp2y = Math.max(-1, Math.min(2, y2));
                                    return ;
                                }
                            }
                            _resetToDefault();
                        }

                        function _resetToDefault() {
                            curveType = "bezier";
                            elasticAmplitude = 1;
                            elasticPeriod = 0.3;
                            bouncesCount = 3;
                            var def = easingPreview.defaultCurve.split(",");
                            if (def.length === 4) {
                                cp1x = parseFloat(def[0]);
                                cp1y = parseFloat(def[1]);
                                cp2x = parseFloat(def[2]);
                                cp2y = parseFloat(def[3]);
                            }
                            easingPreview.curveEdited(easingPreview.formatCurve());
                        }

                        // Format control point values back into a curve string
                        function formatCurve() {
                            return cp1x.toFixed(2) + "," + cp1y.toFixed(2) + "," + cp2x.toFixed(2) + "," + cp2y.toFixed(2);
                        }

                        // Cubic bezier Y at parameter t
                        function cubicBezier(y1, y2, t) {
                            var mt = 1 - t;
                            return 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t;
                        }

                        // Newton's method: find parameter t where Bx(t) = x
                        function solveBezierX(x1, x2, x) {
                            var t = x;
                            for (var i = 0; i < 8; i++) {
                                var mt = 1 - t;
                                var bx = 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t - x;
                                var dbx = 3 * mt * mt * x1 + 6 * mt * t * (x2 - x1) + 3 * t * t * (1 - x2);
                                if (Math.abs(dbx) < 1e-12)
                                    break;

                                t -= bx / dbx;
                                t = Math.max(0, Math.min(1, t));
                            }
                            return t;
                        }

                        // Elastic Out: a * 2^(-10t) * sin((t - s) * 2pi/p) + 1
                        function evaluateElasticOut(t, amp, per) {
                            if (t <= 0)
                                return 0;

                            if (t >= 1)
                                return 1;

                            var a = Math.max(1, amp);
                            var s = per / (2 * Math.PI) * Math.asin(1 / a);
                            return a * Math.pow(2, -10 * t) * Math.sin((t - s) * 2 * Math.PI / per) + 1;
                        }

                        // Generalized Bounce Out: n bounce arcs, amplitude-scaled dip heights
                        function evaluateBounceOut(t, amp, n) {
                            if (t <= 0)
                                return 0;

                            if (t >= 1)
                                return 1;

                            var r = 0.5; // restitution coefficient
                            n = Math.max(1, Math.min(8, n));
                            // Base arc duration so all arcs fit in [0, 1]
                            var S = (1 - Math.pow(r, n)) / (1 - r);
                            var d = 1 / (1 + S);
                            // First half-arc: parabolic rise from 0 to 1
                            if (t < d) {
                                var u0 = t / d;
                                return u0 * u0;
                            }
                            // Subsequent full arcs
                            var tAccum = d;
                            for (var k = 0; k < n; k++) {
                                var dk = d * Math.pow(r, k);
                                if (t < tAccum + dk || k === n - 1) {
                                    var u = (t - tAccum) / dk;
                                    var height = Math.pow(r, 2 * (k + 1));
                                    var dip = 1 - 4 * (u - 0.5) * (u - 0.5);
                                    return 1 - height * amp * dip;
                                }
                                tAccum += dk;
                            }
                            return 1;
                        }

                        // Evaluate the easing for a given progress x in [0,1]
                        function evaluateEasing(x) {
                            if (x <= 0)
                                return 0;

                            if (x >= 1)
                                return 1;

                            var ct = easingPreview.curveType;
                            switch (ct) {
                            case "elastic-out":
                                return evaluateElasticOut(x, easingPreview.elasticAmplitude, easingPreview.elasticPeriod);
                            case "elastic-in":
                                return 1 - evaluateElasticOut(1 - x, easingPreview.elasticAmplitude, easingPreview.elasticPeriod);
                            case "elastic-in-out":
                                if (x < 0.5)
                                    return (1 - evaluateElasticOut(1 - 2 * x, easingPreview.elasticAmplitude, easingPreview.elasticPeriod)) * 0.5;

                                return evaluateElasticOut(2 * x - 1, easingPreview.elasticAmplitude, easingPreview.elasticPeriod) * 0.5 + 0.5;
                            case "bounce-out":
                                return evaluateBounceOut(x, easingPreview.elasticAmplitude, easingPreview.bouncesCount);
                            case "bounce-in":
                                return 1 - evaluateBounceOut(1 - x, easingPreview.elasticAmplitude, easingPreview.bouncesCount);
                            case "bounce-in-out":
                                if (x < 0.5)
                                    return (1 - evaluateBounceOut(1 - 2 * x, easingPreview.elasticAmplitude, easingPreview.bouncesCount)) * 0.5;

                                return evaluateBounceOut(2 * x - 1, easingPreview.elasticAmplitude, easingPreview.bouncesCount) * 0.5 + 0.5;
                            default:
                                break;
                            }
                            // Bezier
                            var t = solveBezierX(cp1x, cp2x, x);
                            return cubicBezier(cp1y, cp2y, t);
                        }

                        // Map a curve Y value to canvas pixel Y
                        function yToCanvas(v, gh) {
                            return canvasPad + (1 - (v - yMin) / (yMax - yMin)) * gh;
                        }

                        // Map canvas pixel Y back to curve Y value
                        function canvasToY(py, gh) {
                            if (gh <= 0)
                                return 0.5;

                            return yMax - (py - canvasPad) / gh * (yMax - yMin);
                        }

                        // Map a curve X value [0,1] to canvas pixel X
                        function xToCanvas(v, gw) {
                            return canvasPad + v * gw;
                        }

                        // Map canvas pixel X back to curve X value
                        function canvasToX(px, gw) {
                            if (gw <= 0)
                                return 0.5;

                            return (px - canvasPad) / gw;
                        }

                        // Label text for current curve (amplitude at 2 decimals matching C++ toString)
                        function curveLabel() {
                            var ct = easingPreview.curveType;
                            if (ct === "bezier")
                                return "cubic-bezier(" + cp1x.toFixed(2) + ", " + cp1y.toFixed(2) + ", " + cp2x.toFixed(2) + ", " + cp2y.toFixed(2) + ")";

                            if (_knownCurveTypes.indexOf(ct) >= 0) {
                                var isElastic = (ct === "elastic-in" || ct === "elastic-out" || ct === "elastic-in-out");
                                if (isElastic)
                                    return ct + "(" + easingPreview.elasticAmplitude.toFixed(2) + ", " + easingPreview.elasticPeriod.toFixed(2) + ")";

                                return ct + "(" + easingPreview.elasticAmplitude.toFixed(2) + ", " + easingPreview.bouncesCount + ")";
                            }
                            return ct;
                        }

                        function replay() {
                            if (!easingPreview.previewEnabled)
                                return ;

                            animTimer.stop();
                            animBox.x = 0;
                            curveCanvas.requestPaint();
                            replayDelay.restart();
                        }

                        Accessible.name: i18n("Animation easing curve editor")
                        Accessible.description: i18n("Drag the control points to customize the animation curve. Click to replay the preview.")
                        Layout.fillWidth: true
                        Layout.maximumWidth: 500
                        Layout.alignment: Qt.AlignHCenter
                        implicitHeight: easingLayout.implicitHeight
                        implicitWidth: 400
                        opacity: animationsEnabledCheck.checked ? 1 : 0.4
                        onCurveChanged: {
                            if (!dragArea.activeHandle) {
                                parseCurve();
                                curveCanvas.requestPaint();
                                replay();
                            }
                        }
                        Component.onCompleted: {
                            parseCurve();
                            curveCanvas.requestPaint();
                            Qt.callLater(replay);
                        }

                        Timer {
                            id: replayDelay

                            interval: 80
                            onTriggered: {
                                animTimer.elapsed = 0;
                                animTimer.lastTime = Date.now();
                                animTimer.start();
                            }
                        }

                        Timer {
                            id: animTimer

                            property real elapsed: 0
                            property real lastTime: 0

                            interval: 16 // ~60fps
                            repeat: true
                            onTriggered: {
                                var now = Date.now();
                                elapsed += (now - lastTime);
                                lastTime = now;
                                var totalDuration = Math.max(easingPreview.animationDuration, 50);
                                var progress = Math.min(elapsed / totalDuration, 1);
                                var easedProgress = easingPreview.evaluateEasing(progress);
                                var trackWidth = Math.max(0, animBox.parent.width - easingPreview.boxSize);
                                animBox.x = Math.max(0, Math.min(trackWidth, easedProgress * trackWidth));
                                if (progress >= 1) {
                                    animBox.x = trackWidth;
                                    animTimer.stop();
                                }
                            }
                        }

                        ColumnLayout {
                            id: easingLayout

                            anchors.fill: parent
                            spacing: 0

                            // Interactive curve canvas
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: easingPreview.canvasHeight
                                ToolTip.visible: dragArea.containsMouse
                                ToolTip.text: easingPreview.isBezier ? i18n("Drag control points to customize the animation curve. Click to replay.") : i18n("Click to replay the animation preview.")
                                ToolTip.delay: 500

                                Canvas {
                                    id: curveCanvas

                                    // Control point handles
                                    function drawHandle(ctx, cx, cy, active) {
                                        ctx.beginPath();
                                        ctx.arc(cx, cy, easingPreview.handleRadius, 0, 2 * Math.PI);
                                        ctx.fillStyle = active ? Kirigami.Theme.highlightColor : Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.6);
                                        ctx.fill();
                                        ctx.strokeStyle = Qt.rgba(Kirigami.Theme.highlightedTextColor.r, Kirigami.Theme.highlightedTextColor.g, Kirigami.Theme.highlightedTextColor.b, 0.8);
                                        ctx.lineWidth = active ? 2.5 : 1.5;
                                        ctx.stroke();
                                    }

                                    anchors.fill: parent
                                    onPaint: {
                                        var ctx = getContext("2d");
                                        var w = width, h = height;
                                        var pad = easingPreview.canvasPad;
                                        var gw = w - pad * 2;
                                        var gh = h - pad * 2;
                                        ctx.clearRect(0, 0, w, h);
                                        // Subtle background fill
                                        ctx.fillStyle = Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, 0.4);
                                        ctx.fillRect(0, 0, w, h);
                                        // Reference lines at y=0 and y=1
                                        var refColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.25);
                                        ctx.strokeStyle = refColor;
                                        ctx.lineWidth = 1;
                                        ctx.setLineDash([]);
                                        var yZero = easingPreview.yToCanvas(0, gh);
                                        var yOne = easingPreview.yToCanvas(1, gh);
                                        ctx.beginPath();
                                        ctx.moveTo(pad, yZero);
                                        ctx.lineTo(pad + gw, yZero);
                                        ctx.stroke();
                                        ctx.beginPath();
                                        ctx.moveTo(pad, yOne);
                                        ctx.lineTo(pad + gw, yOne);
                                        ctx.stroke();
                                        // Grid lines (vertical, dashed)
                                        ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.1);
                                        ctx.setLineDash([2, 4]);
                                        for (var j = 0; j <= 4; j++) {
                                            var gx = pad + gw * j / 4;
                                            ctx.beginPath();
                                            ctx.moveTo(gx, pad);
                                            ctx.lineTo(gx, pad + gh);
                                            ctx.stroke();
                                        }
                                        ctx.setLineDash([]);
                                        // Linear diagonal reference (P0 to P3)
                                        ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
                                        ctx.lineWidth = 1;
                                        ctx.beginPath();
                                        ctx.moveTo(pad, yZero);
                                        ctx.lineTo(pad + gw, yOne);
                                        ctx.stroke();
                                        if (easingPreview.isBezier) {
                                            // === BEZIER: control point handles + native bezierCurveTo ===
                                            var p0x = pad, p0y = yZero;
                                            var p3x = pad + gw, p3y = yOne;
                                            var h1x = easingPreview.xToCanvas(easingPreview.cp1x, gw);
                                            var h1y = easingPreview.yToCanvas(easingPreview.cp1y, gh);
                                            var h2x = easingPreview.xToCanvas(easingPreview.cp2x, gw);
                                            var h2y = easingPreview.yToCanvas(easingPreview.cp2y, gh);
                                            // Handle connection lines (dashed): P0->P1 and P3->P2
                                            ctx.strokeStyle = Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.5);
                                            ctx.lineWidth = 1.5;
                                            ctx.setLineDash([4, 3]);
                                            ctx.beginPath();
                                            ctx.moveTo(p0x, p0y);
                                            ctx.lineTo(h1x, h1y);
                                            ctx.stroke();
                                            ctx.beginPath();
                                            ctx.moveTo(p3x, p3y);
                                            ctx.lineTo(h2x, h2y);
                                            ctx.stroke();
                                            ctx.setLineDash([]);
                                            // Cubic bezier curve (highlighted)
                                            ctx.strokeStyle = Kirigami.Theme.highlightColor;
                                            ctx.lineWidth = 2.5;
                                            ctx.beginPath();
                                            ctx.moveTo(p0x, p0y);
                                            ctx.bezierCurveTo(h1x, h1y, h2x, h2y, p3x, p3y);
                                            ctx.stroke();
                                            drawHandle(ctx, h1x, h1y, dragArea.activeHandle === 1);
                                            drawHandle(ctx, h2x, h2y, dragArea.activeHandle === 2);
                                            // Anchor dots at P0 and P3
                                            ctx.fillStyle = Kirigami.Theme.disabledTextColor;
                                            ctx.beginPath();
                                            ctx.arc(p0x, p0y, 3, 0, 2 * Math.PI);
                                            ctx.fill();
                                            ctx.beginPath();
                                            ctx.arc(p3x, p3y, 3, 0, 2 * Math.PI);
                                            ctx.fill();
                                        } else {
                                            // === NON-BEZIER: polyline sampled from evaluateEasing ===
                                            var nSamples = 200;
                                            ctx.strokeStyle = Kirigami.Theme.highlightColor;
                                            ctx.lineWidth = 2.5;
                                            ctx.beginPath();
                                            for (var si = 0; si <= nSamples; si++) {
                                                var sx = si / nSamples;
                                                var sy = easingPreview.evaluateEasing(sx);
                                                var px = easingPreview.xToCanvas(sx, gw);
                                                var py = easingPreview.yToCanvas(sy, gh);
                                                if (si === 0)
                                                    ctx.moveTo(px, py);
                                                else
                                                    ctx.lineTo(px, py);
                                            }
                                            ctx.stroke();
                                            // Anchor dots at endpoints
                                            ctx.fillStyle = Kirigami.Theme.disabledTextColor;
                                            ctx.beginPath();
                                            ctx.arc(pad, yZero, 3, 0, 2 * Math.PI);
                                            ctx.fill();
                                            ctx.beginPath();
                                            ctx.arc(pad + gw, yOne, 3, 0, 2 * Math.PI);
                                            ctx.fill();
                                        }
                                        // Axis labels
                                        ctx.fillStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4);
                                        ctx.font = "9px sans-serif";
                                        ctx.textAlign = "right";
                                        ctx.fillText("0", pad - 3, yZero + 3);
                                        ctx.fillText("1", pad - 3, yOne + 3);
                                    }
                                }

                                // Single MouseArea for all handle interaction
                                MouseArea {
                                    // NOTE: We DON'T call curveEdited here to avoid Hammering config/backend during drag

                                    id: dragArea

                                    // 0 = none, 1 = P1, 2 = P2
                                    property int activeHandle: 0
                                    property int hoveredHandle: 0
                                    property bool dragStarted: false
                                    property point pressPos: Qt.point(0, 0)

                                    function handleDist(mx, my, cpx, cpy) {
                                        var gw = curveCanvas.width - easingPreview.canvasPad * 2;
                                        var gh = curveCanvas.height - easingPreview.canvasPad * 2;
                                        if (gw <= 0 || gh <= 0)
                                            return Infinity;

                                        var hx = easingPreview.xToCanvas(cpx, gw);
                                        var hy = easingPreview.yToCanvas(cpy, gh);
                                        return Math.sqrt((mx - hx) * (mx - hx) + (my - hy) * (my - hy));
                                    }

                                    function nearestHandle(mx, my) {
                                        if (!easingPreview.isBezier)
                                            return 0;

                                        var d1 = handleDist(mx, my, easingPreview.cp1x, easingPreview.cp1y);
                                        var d2 = handleDist(mx, my, easingPreview.cp2x, easingPreview.cp2y);
                                        var hitRadius = easingPreview.handleHitRadius;
                                        if (d1 <= hitRadius && d1 <= d2)
                                            return 1;

                                        if (d2 <= hitRadius)
                                            return 2;

                                        return 0;
                                    }

                                    anchors.fill: parent
                                    hoverEnabled: true
                                    preventStealing: true // Crucial: prevents parent ScrollView from stealing vertical drag
                                    cursorShape: easingPreview.isBezier ? (activeHandle ? Qt.ClosedHandCursor : (hoveredHandle ? Qt.OpenHandCursor : Qt.ArrowCursor)) : Qt.ArrowCursor
                                    onPositionChanged: (mouse) => {
                                        if (activeHandle) {
                                            // Start drag immediately or with very small threshold for reliability
                                            if (!dragStarted) {
                                                var dist = Math.sqrt(Math.pow(mouse.x - pressPos.x, 2) + Math.pow(mouse.y - pressPos.y, 2));
                                                if (dist > 2)
                                                    dragStarted = true;

                                            }
                                            if (dragStarted) {
                                                var gw = curveCanvas.width - easingPreview.canvasPad * 2;
                                                var gh = curveCanvas.height - easingPreview.canvasPad * 2;
                                                if (gw <= 0 || gh <= 0)
                                                    return ;

                                                var nx = Math.max(0, Math.min(1, easingPreview.canvasToX(mouse.x, gw)));
                                                var ny = Math.max(easingPreview.yMin, Math.min(easingPreview.yMax, easingPreview.canvasToY(mouse.y, gh)));
                                                // High-precision update during drag for smoothness
                                                if (activeHandle === 1) {
                                                    easingPreview.cp1x = nx;
                                                    easingPreview.cp1y = ny;
                                                } else {
                                                    easingPreview.cp2x = nx;
                                                    easingPreview.cp2y = ny;
                                                }
                                                curveCanvas.requestPaint();
                                            }
                                        } else {
                                            hoveredHandle = nearestHandle(mouse.x, mouse.y);
                                        }
                                    }
                                    onPressed: (mouse) => {
                                        pressPos = Qt.point(mouse.x, mouse.y);
                                        dragStarted = false;
                                        activeHandle = nearestHandle(mouse.x, mouse.y);
                                    }
                                    onReleased: {
                                        if (activeHandle) {
                                            // Snap to 0.01 increments only on release for clean config values
                                            easingPreview.cp1x = Math.round(easingPreview.cp1x * 100) / 100;
                                            easingPreview.cp1y = Math.round(easingPreview.cp1y * 100) / 100;
                                            easingPreview.cp2x = Math.round(easingPreview.cp2x * 100) / 100;
                                            easingPreview.cp2y = Math.round(easingPreview.cp2y * 100) / 100;
                                            activeHandle = 0;
                                            easingPreview.replay();
                                            // Emit the update only once at the end of the drag to ensure stability
                                            easingPreview.curveEdited(easingPreview.formatCurve());
                                        }
                                    }
                                    onCanceled: {
                                        activeHandle = 0;
                                        dragStarted = false;
                                    }
                                    onClicked: {
                                        // Replay on click if it wasn't a significant drag
                                        if (!dragStarted)
                                            easingPreview.replay();

                                    }
                                }

                            }

                            // Curve value display
                            Label {
                                Layout.fillWidth: true
                                Layout.leftMargin: easingPreview.canvasPad
                                Layout.rightMargin: easingPreview.canvasPad
                                text: easingPreview.curveLabel()
                                font: Kirigami.Theme.fixedWidthFont
                                color: Kirigami.Theme.disabledTextColor
                                horizontalAlignment: Text.AlignHCenter
                            }

                            // Animated box track
                            Item {
                                Layout.fillWidth: true
                                Layout.preferredHeight: easingPreview.boxTrackHeight

                                // Track rail
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: easingPreview.boxSize / 2
                                    anchors.rightMargin: easingPreview.boxSize / 2
                                    height: 3
                                    radius: 1.5
                                    color: Kirigami.Theme.disabledTextColor
                                }

                                // Start marker
                                Rectangle {
                                    x: easingPreview.boxSize / 2 - 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 2
                                    height: 12
                                    radius: 1
                                    color: Kirigami.Theme.disabledTextColor
                                    opacity: 0.4
                                }

                                // End marker
                                Rectangle {
                                    x: parent.width - easingPreview.boxSize / 2 - 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: 2
                                    height: 12
                                    radius: 1
                                    color: Kirigami.Theme.disabledTextColor
                                    opacity: 0.4
                                }

                                // Animated box
                                Rectangle {
                                    id: animBox

                                    width: easingPreview.boxSize
                                    height: easingPreview.boxSize
                                    radius: easingPreview.boxSize / 5
                                    y: (parent.height - height) / 2
                                    x: 0
                                    color: Kirigami.Theme.highlightColor
                                }

                            }

                        }

                    }

                    Kirigami.Separator {
                        Layout.fillWidth: true
                    }

                    // ═══════════════════════════════════════════════════════════
                    // EasingSettings (inlined from EasingSettings.qml)
                    // ═══════════════════════════════════════════════════════════
                    Kirigami.FormLayout {
                        id: easingRoot

                        Layout.fillWidth: true

                        // Easing style + direction selector (two dropdowns)
                        // Curve lookup table: styles x directions -> bezier/named curve strings
                        QtObject {
                            id: easingData

                            // Style definitions: user-friendly name, technical name, curves per direction
                            readonly property var styles: [{
                                "label": i18n("Custom"),
                                "key": "custom"
                            }, {
                                "label": i18n("Linear"),
                                "key": "linear"
                            }, {
                                "label": i18n("Gentle (Sine)"),
                                "key": "sine"
                            }, {
                                "label": i18n("Smooth (Quad)"),
                                "key": "quad"
                            }, {
                                "label": i18n("Standard (Cubic)"),
                                "key": "cubic"
                            }, {
                                "label": i18n("Snappy (Quart)"),
                                "key": "quart"
                            }, {
                                "label": i18n("Sharp (Quint)"),
                                "key": "quint"
                            }, {
                                "label": i18n("Aggressive (Expo)"),
                                "key": "expo"
                            }, {
                                "label": i18n("Circular (Circ)"),
                                "key": "circ"
                            }, {
                                "label": i18n("Overshoot (Back)"),
                                "key": "back"
                            }, {
                                "label": i18n("Elastic"),
                                "key": "elastic"
                            }, {
                                "label": i18n("Bounce"),
                                "key": "bounce"
                            }]
                            readonly property var directions: [{
                                "label": i18n("Ease In"),
                                "key": "in"
                            }, {
                                "label": i18n("Ease Out"),
                                "key": "out"
                            }, {
                                "label": i18n("Ease In-Out"),
                                "key": "in-out"
                            }]
                            // Bezier curves: style -> { in, out, in-out }
                            readonly property var curves: ({
                                "linear": {
                                    "in": "0.00,0.00,1.00,1.00",
                                    "out": "0.00,0.00,1.00,1.00",
                                    "in-out": "0.00,0.00,1.00,1.00"
                                },
                                "sine": {
                                    "in": "0.12,0.00,0.39,0.00",
                                    "out": "0.61,1.00,0.88,1.00",
                                    "in-out": "0.37,0.00,0.63,1.00"
                                },
                                "quad": {
                                    "in": "0.11,0.00,0.50,0.00",
                                    "out": "0.50,1.00,0.89,1.00",
                                    "in-out": "0.45,0.00,0.55,1.00"
                                },
                                "cubic": {
                                    "in": "0.32,0.00,0.67,0.00",
                                    "out": "0.33,1.00,0.68,1.00",
                                    "in-out": "0.65,0.00,0.35,1.00"
                                },
                                "quart": {
                                    "in": "0.50,0.00,0.75,0.00",
                                    "out": "0.25,1.00,0.50,1.00",
                                    "in-out": "0.76,0.00,0.24,1.00"
                                },
                                "quint": {
                                    "in": "0.64,0.00,0.78,0.00",
                                    "out": "0.23,1.00,0.32,1.00",
                                    "in-out": "0.83,0.00,0.17,1.00"
                                },
                                "expo": {
                                    "in": "0.70,0.00,0.84,0.00",
                                    "out": "0.16,1.00,0.30,1.00",
                                    "in-out": "0.87,0.00,0.13,1.00"
                                },
                                "circ": {
                                    "in": "0.55,0.00,1.00,0.45",
                                    "out": "0.00,0.55,0.45,1.00",
                                    "in-out": "0.85,0.00,0.15,1.00"
                                },
                                "back": {
                                    "in": "0.36,0.00,0.66,-0.56",
                                    "out": "0.18,0.89,0.32,1.28",
                                    "in-out": "0.68,-0.55,0.27,1.55"
                                },
                                "elastic": {
                                    "in": "elastic-in:1.0,0.30",
                                    "out": "elastic-out:1.0,0.30",
                                    "in-out": "elastic-in-out:1.0,0.30"
                                },
                                "bounce": {
                                    "in": "bounce-in:1.0,3",
                                    "out": "bounce-out:1.0,3",
                                    "in-out": "bounce-in-out:1.0,3"
                                }
                            })

                            // Normalize curve for comparison (2 decimal bezier, canonical named)
                            function normalizeCurve(curve) {
                                if (!curve || curve === "")
                                    return "";

                                var hasLetter = false;
                                for (var i = 0; i < curve.length; i++) {
                                    var c = curve.charAt(i);
                                    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                                        if (c !== 'e' && c !== 'E') {
                                            hasLetter = true;
                                            break;
                                        }
                                    }
                                }
                                if (hasLetter) {
                                    var colonIdx = curve.indexOf(':');
                                    var name = colonIdx >= 0 ? curve.substring(0, colonIdx).trim() : curve.trim();
                                    var params = colonIdx >= 0 ? curve.substring(colonIdx + 1).trim() : "";
                                    var isElastic = name.startsWith("elastic");
                                    var isBounce = name.startsWith("bounce");
                                    if (params) {
                                        var parts = params.split(",");
                                        var amp = parts.length >= 1 ? parseFloat(parts[0]) : 1;
                                        if (!isFinite(amp))
                                            amp = 1;

                                        if (isElastic) {
                                            var per = parts.length >= 2 ? parseFloat(parts[1]) : 0.3;
                                            if (!isFinite(per))
                                                per = 0.3;

                                            return name + ":" + amp.toFixed(2) + "," + per.toFixed(2);
                                        }
                                        if (isBounce) {
                                            var bn = parts.length >= 2 ? Math.round(parseFloat(parts[1])) : 3;
                                            if (!isFinite(bn))
                                                bn = 3;

                                            return name + ":" + amp.toFixed(2) + "," + bn;
                                        }
                                    }
                                    if (isBounce)
                                        return name + ":1.00,3";

                                    if (isElastic)
                                        return name + ":1.00,0.30";

                                    return name;
                                }
                                var bparts = curve.split(",");
                                if (bparts.length !== 4)
                                    return "";

                                var x1 = parseFloat(bparts[0]), y1 = parseFloat(bparts[1]);
                                var x2 = parseFloat(bparts[2]), y2 = parseFloat(bparts[3]);
                                if (!isFinite(x1) || !isFinite(y1) || !isFinite(x2) || !isFinite(y2))
                                    return "";

                                return x1.toFixed(2) + "," + y1.toFixed(2) + "," + x2.toFixed(2) + "," + y2.toFixed(2);
                            }

                            // Reverse-lookup: curve string -> { styleIndex, dirIndex }
                            function findIndices(curve) {
                                var norm = normalizeCurve(curve);
                                if (norm === "")
                                    return {
                                    "styleIndex": 0,
                                    "dirIndex": 1
                                };

                                // Custom, Ease Out
                                for (var si = 1; si < styles.length; si++) {
                                    var key = styles[si].key;
                                    if (!curves[key])
                                        continue;

                                    for (var di = 0; di < directions.length; di++) {
                                        var dirKey = directions[di].key;
                                        if (normalizeCurve(curves[key][dirKey]) === norm)
                                            return {
                                            "styleIndex": si,
                                            "dirIndex": di
                                        };

                                    }
                                }
                                return {
                                    "styleIndex": 0,
                                    "dirIndex": 1
                                }; // Custom
                            }

                            // Build curve string from style + direction indices
                            function curveForSelection(styleIndex, dirIndex) {
                                if (styleIndex <= 0 || styleIndex >= styles.length)
                                    return "";

                                var key = styles[styleIndex].key;
                                var dirKey = directions[dirIndex].key;
                                return curves[key] ? curves[key][dirKey] : "";
                            }

                        }

                        // Style selector
                        ComboBox {
                            id: easingStyleCombo

                            property bool updating: false

                            Kirigami.FormData.label: i18n("Style:")
                            enabled: animationsEnabledCheck.checked
                            model: easingData.styles.map((s) => {
                                return s.label;
                            })
                            currentIndex: easingData.findIndices(kcm.animationEasingCurve).styleIndex
                            onActivated: (index) => {
                                if (updating)
                                    return ;

                                if (index <= 0)
                                    return ;

                                // Custom -- don't change curve
                                var curve = easingData.curveForSelection(index, easingDirectionCombo.currentIndex);
                                if (curve)
                                    kcm.animationEasingCurve = curve;

                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Animation curve style -- controls how acceleration feels")

                            Connections {
                                function onAnimationEasingCurveChanged() {
                                    easingStyleCombo.updating = true;
                                    var idx = easingData.findIndices(kcm.animationEasingCurve);
                                    easingStyleCombo.currentIndex = idx.styleIndex;
                                    easingStyleCombo.updating = false;
                                }

                                target: kcm
                            }

                        }

                        // Direction selector
                        ComboBox {
                            id: easingDirectionCombo

                            property bool updating: false

                            Kirigami.FormData.label: i18n("Direction:")
                            enabled: animationsEnabledCheck.checked && easingStyleCombo.currentIndex > 0
                            opacity: easingStyleCombo.currentIndex > 0 ? 1 : 0.4
                            model: easingData.directions.map((d) => {
                                return d.label;
                            })
                            currentIndex: easingData.findIndices(kcm.animationEasingCurve).dirIndex
                            onActivated: (index) => {
                                if (updating)
                                    return ;

                                var styleIdx = easingStyleCombo.currentIndex;
                                if (styleIdx <= 0)
                                    return ;

                                var curve = easingData.curveForSelection(styleIdx, index);
                                if (curve)
                                    kcm.animationEasingCurve = curve;

                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("Ease In accelerates from rest, Ease Out decelerates to rest, In-Out does both")

                            Connections {
                                function onAnimationEasingCurveChanged() {
                                    easingDirectionCombo.updating = true;
                                    var idx = easingData.findIndices(kcm.animationEasingCurve);
                                    easingDirectionCombo.currentIndex = idx.dirIndex;
                                    easingDirectionCombo.updating = false;
                                }

                                target: kcm
                            }

                        }

                        // Amplitude slider (visible for elastic and bounce presets)
                        RowLayout {
                            id: amplitudeRow

                            readonly property bool isElastic: easingPreview.curveType === "elastic-in" || easingPreview.curveType === "elastic-out" || easingPreview.curveType === "elastic-in-out"
                            readonly property bool isBounce: easingPreview.curveType === "bounce-in" || easingPreview.curveType === "bounce-out" || easingPreview.curveType === "bounce-in-out"

                            Kirigami.FormData.label: i18n("Amplitude:")
                            visible: isElastic || isBounce
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: elasticAmplitudeSlider

                                Layout.preferredWidth: root.sliderPreferredWidth
                                from: amplitudeRow.isElastic ? 1 : 0.5
                                to: 3
                                stepSize: 0.1
                                value: easingPreview.elasticAmplitude
                                onMoved: {
                                    var ct = easingPreview.curveType;
                                    var amp = value.toFixed(2);
                                    if (amplitudeRow.isElastic) {
                                        var per = easingPreview.elasticPeriod.toFixed(2);
                                        kcm.animationEasingCurve = ct + ":" + amp + "," + per;
                                    } else {
                                        kcm.animationEasingCurve = ct + ":" + amp + "," + easingPreview.bouncesCount;
                                    }
                                }
                                Accessible.name: i18n("Amplitude")
                                ToolTip.visible: hovered
                                ToolTip.text: amplitudeRow.isBounce ? i18n("Controls the bounce height -- higher values exaggerate bounces, lower values flatten them (1.0 = standard)") : i18n("Controls the overshoot intensity of the elastic animation (1.0 = standard)")
                            }

                            Label {
                                text: elasticAmplitudeSlider.value.toFixed(1)
                                Layout.preferredWidth: root.sliderValueLabelWidth
                            }

                        }

                        // Bounce count slider (visible for bounce presets)
                        RowLayout {
                            Kirigami.FormData.label: i18n("Bounces:")
                            visible: amplitudeRow.isBounce
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: bouncesSlider

                                Layout.preferredWidth: root.sliderPreferredWidth
                                from: 1
                                to: 8
                                stepSize: 1
                                value: easingPreview.bouncesCount
                                onMoved: {
                                    var ct = easingPreview.curveType;
                                    var amp = easingPreview.elasticAmplitude.toFixed(2);
                                    kcm.animationEasingCurve = ct + ":" + amp + "," + Math.round(value);
                                }
                                Accessible.name: i18n("Number of bounces")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Number of bounces before settling -- fewer bounces feel snappier, more feel bouncier")
                            }

                            Label {
                                text: Math.round(bouncesSlider.value).toString()
                                Layout.preferredWidth: root.sliderValueLabelWidth
                            }

                        }

                        // Elastic period slider
                        RowLayout {
                            Kirigami.FormData.label: i18n("Period:")
                            visible: easingPreview.curveType === "elastic-in" || easingPreview.curveType === "elastic-out" || easingPreview.curveType === "elastic-in-out"
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: elasticPeriodSlider

                                Layout.preferredWidth: root.sliderPreferredWidth
                                from: 0.1
                                to: 1
                                stepSize: 0.05
                                value: easingPreview.elasticPeriod
                                onMoved: {
                                    var ct = easingPreview.curveType;
                                    var amp = easingPreview.elasticAmplitude.toFixed(2);
                                    var per = value.toFixed(2);
                                    kcm.animationEasingCurve = ct + ":" + amp + "," + per;
                                }
                                Accessible.name: i18n("Elastic period")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Controls the oscillation frequency -- lower values produce tighter, faster bounces")
                            }

                            Label {
                                text: elasticPeriodSlider.value.toFixed(2)
                                Layout.preferredWidth: root.sliderValueLabelWidth
                            }

                        }

                        // Duration slider
                        RowLayout {
                            Kirigami.FormData.label: i18n("Duration:")
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                id: animationDurationSlider

                                Layout.preferredWidth: root.sliderPreferredWidth
                                from: 50
                                to: 500
                                stepSize: 10
                                value: kcm.animationDuration
                                onMoved: kcm.animationDuration = Math.round(value)
                                Accessible.name: i18n("Animation duration")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("How long window animations take to complete (milliseconds)")
                            }

                            Label {
                                text: Math.round(animationDurationSlider.value) + " ms"
                                Layout.preferredWidth: root.sliderValueLabelWidth + 15
                            }

                        }

                        // Sequence mode (all at once vs one by one)
                        ComboBox {
                            Kirigami.FormData.label: i18n("Multiple windows:")
                            enabled: animationsEnabledCheck.checked
                            model: [i18n("Animate all at once"), i18n("Animate one by one (zone order)")]
                            currentIndex: kcm.animationSequenceMode
                            onActivated: (index) => {
                                return kcm.animationSequenceMode = index;
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: i18n("When moving multiple windows (resnap, snap all, autotile, etc.), animate them all together or one after another in zone order.")
                        }

                        // Stagger interval (only relevant when one by one)
                        RowLayout {
                            Kirigami.FormData.label: i18n("Delay between windows:")
                            visible: kcm.animationSequenceMode === 1
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            Slider {
                                Layout.preferredWidth: root.sliderPreferredWidth
                                from: 10
                                to: 500
                                stepSize: 10
                                value: kcm.animationStaggerInterval
                                onMoved: kcm.animationStaggerInterval = Math.round(value)
                                Accessible.name: i18n("Delay between each window starting its animation")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("When animating one by one: milliseconds between each window starting. Lower values create a fast cascading effect with overlapping animations.")
                            }

                            Label {
                                text: i18n("%1 ms", kcm.animationStaggerInterval)
                                Layout.preferredWidth: root.sliderValueLabelWidth + 15
                            }

                        }

                        // Minimum distance threshold
                        RowLayout {
                            Kirigami.FormData.label: i18n("Minimum distance:")
                            enabled: animationsEnabledCheck.checked
                            spacing: Kirigami.Units.smallSpacing

                            SpinBox {
                                from: 0
                                to: 200
                                stepSize: 5
                                value: kcm.animationMinDistance
                                onValueModified: kcm.animationMinDistance = value
                                Accessible.name: i18n("Minimum distance")
                                ToolTip.visible: hovered
                                ToolTip.text: i18n("Skip animation when the geometry change is smaller than this many pixels. Prevents jittery micro-animations.")
                            }

                            Label {
                                text: i18n("px")
                            }

                            Label {
                                text: kcm.animationMinDistance === 0 ? i18n("(always animate)") : ""
                                opacity: 0.6
                                font.italic: true
                            }

                        }

                    }

                }

            }

        }

        // ═══════════════════════════════════════════════════════════════════════
        // ON-SCREEN DISPLAY CARD (inlined from OsdCard.qml)
        // ═══════════════════════════════════════════════════════════════════════
        Item {
            Layout.fillWidth: true
            implicitHeight: osdCard.implicitHeight

            Kirigami.Card {
                id: osdCard

                anchors.fill: parent

                header: Kirigami.Heading {
                    level: 3
                    text: i18n("On-Screen Display")
                    padding: Kirigami.Units.smallSpacing
                }

                contentItem: Kirigami.FormLayout {
                    CheckBox {
                        id: showOsdCheckbox

                        Kirigami.FormData.label: i18n("Layout switch:")
                        text: i18n("Show OSD when switching layouts")
                        checked: kcm.showOsdOnLayoutSwitch
                        onToggled: kcm.showOsdOnLayoutSwitch = checked
                    }

                    CheckBox {
                        Kirigami.FormData.label: i18n("Keyboard navigation:")
                        text: i18n("Show OSD when using keyboard navigation")
                        checked: kcm.showNavigationOsd
                        onToggled: kcm.showNavigationOsd = checked
                    }

                    ComboBox {
                        id: osdStyleCombo

                        readonly property int osdStyleNone: 0
                        readonly property int osdStyleText: 1
                        readonly property int osdStylePreview: 2

                        Kirigami.FormData.label: i18n("OSD style:")
                        enabled: showOsdCheckbox.checked || kcm.showNavigationOsd
                        currentIndex: Math.max(0, Math.min(kcm.osdStyle, 2))
                        model: [i18n("None"), i18n("Text only"), i18n("Visual preview")]
                        onActivated: (index) => {
                            kcm.osdStyle = index;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: currentIndex === 0 ? i18n("No OSD shown. Enable layout switch or keyboard navigation above to show OSD.") : (currentIndex === 1 ? i18n("Show layout name as text only") : i18n("Show visual layout preview"))
                    }

                    ComboBox {
                        Kirigami.FormData.label: i18n("Overlay style:")
                        currentIndex: Math.max(0, Math.min(kcm.overlayDisplayMode, 1))
                        model: [i18n("Full zone highlight"), i18n("Compact preview")]
                        onActivated: (index) => {
                            kcm.overlayDisplayMode = index;
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: currentIndex === 0 ? i18n("Highlight each zone as a full-size translucent rectangle while dragging") : i18n("Show a small layout thumbnail inside each zone while dragging")
                    }

                }

            }

        }

    }

    // Wire up easingPreview.curve to kcm.animationEasingCurve bidirectionally
    Connections {
        function onAnimationEasingCurveChanged() {
            easingPreview.curve = kcm.animationEasingCurve;
        }

        target: kcm
    }

    Connections {
        function onCurveEdited(newCurve) {
            kcm.animationEasingCurve = newCurve;
        }

        target: easingPreview
    }

}
