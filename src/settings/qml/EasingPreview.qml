// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import "EasingCurve.js" as Easing
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Easing curve editor with draggable control points and named curve support
 *
 * Supports cubic bezier (interactive drag handles), elastic, and bounce curves.
 * Bezier: "x1,y1,x2,y2", Named: "elastic-out:1.0,0.3" or "bounce-out".
 * Detection: if string contains a letter -> named curve; otherwise -> bezier.
 */
Item {
    id: root

    property string curve: root.defaultCurve
    property int animationDuration: 150
    property bool previewEnabled: true
    readonly property int canvasHeight: Kirigami.Units.gridUnit * 15
    readonly property int boxTrackHeight: Kirigami.Units.gridUnit * 2
    readonly property int boxSize: Math.round(Kirigami.Units.gridUnit * 1.5)
    readonly property int canvasPad: Math.round(Kirigami.Units.gridUnit * 1.5)
    readonly property int handleRadius: Math.round(Kirigami.Units.gridUnit * 0.6)
    readonly property int handleHitRadius: Math.round(Kirigami.Units.gridUnit * 1.1)
    // Y range: [-1, 2] mapped to canvas, allowing overshoot
    readonly property real yMin: -1
    readonly property real yMax: 2
    // Default curve string (Ease Out Cubic) — matches ConfigDefaults
    readonly property string defaultCurve: "0.33,1.00,0.68,1.00"
    // Parsed control point values (internal state, bezier only)
    property real cp1x: 0.33
    property real cp1y: 1
    property real cp2x: 0.68
    property real cp2y: 1
    // Named curve state — set imperatively by parseCurve(), not via binding,
    // to avoid reading stale _parsedCurveType before parseCurve() runs.
    property string curveType: "bezier"
    property real curveAmplitude: 1
    property real elasticPeriod: 0.3
    property int bouncesCount: 3
    property bool _initializing: true
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
        var str = root.curve;
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
            curveAmplitude = 1;
            elasticPeriod = 0.3;
            bouncesCount = 3;
            if (params) {
                var parts = params.split(",");
                if (parts.length >= 1) {
                    var a = parseFloat(parts[0]);
                    if (isFinite(a))
                        curveAmplitude = Math.max(0.5, Math.min(3, a));

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
        curveAmplitude = 1;
        elasticPeriod = 0.3;
        bouncesCount = 3;
        var def = root.defaultCurve.split(",");
        if (def.length === 4) {
            cp1x = parseFloat(def[0]);
            cp1y = parseFloat(def[1]);
            cp2x = parseFloat(def[2]);
            cp2y = parseFloat(def[3]);
        }
        if (!root._initializing)
            root.curveEdited(root.formatCurve());

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

    // Evaluate the easing for a given progress x in [0,1].
    // Named curves (elastic/bounce) delegate to EasingCurve.js; bezier uses
    // the local solveBezierX + cubicBezier for interactive drag-handle fidelity.
    function evaluateEasing(x) {
        if (x <= 0)
            return 0;

        if (x >= 1)
            return 1;

        var ct = root.curveType;
        // Named curves: build the curve string and delegate to EasingCurve.js
        if (ct !== "bezier") {
            var curveStr = ct;
            var isElastic = (ct.indexOf("elastic") >= 0);
            if (isElastic)
                curveStr = ct + ":" + root.curveAmplitude.toFixed(2) + "," + root.elasticPeriod.toFixed(2);
            else if (ct.indexOf("bounce") >= 0)
                curveStr = ct + ":" + root.curveAmplitude.toFixed(2) + "," + root.bouncesCount;
            return Easing.evaluate(x, curveStr);
        }
        // Bezier: use local solver for drag-handle precision
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
        var ct = root.curveType;
        if (ct === "bezier")
            return "cubic-bezier(" + cp1x.toFixed(2) + ", " + cp1y.toFixed(2) + ", " + cp2x.toFixed(2) + ", " + cp2y.toFixed(2) + ")";

        if (_knownCurveTypes.indexOf(ct) >= 0) {
            var isElastic = (ct === "elastic-in" || ct === "elastic-out" || ct === "elastic-in-out");
            if (isElastic)
                return ct + "(" + root.curveAmplitude.toFixed(2) + ", " + root.elasticPeriod.toFixed(2) + ")";

            return ct + "(" + root.curveAmplitude.toFixed(2) + ", " + root.bouncesCount + ")";
        }
        return ct;
    }

    function replay() {
        if (!root.previewEnabled)
            return ;

        animTimer.stop();
        boxTrack.animBox.x = 0;
        curveCanvas.requestPaint();
        replayDelay.restart();
    }

    Accessible.name: i18n("Animation easing curve editor")
    Accessible.role: Accessible.Graphic
    Accessible.description: i18n("Drag the control points to customize the animation curve. Click to replay the preview.")
    implicitHeight: layout.implicitHeight
    implicitWidth: Kirigami.Units.gridUnit * 25
    onCurveChanged: {
        if (!dragArea.activeHandle) {
            parseCurve();
            curveCanvas.requestPaint();
            replay();
        }
    }
    Component.onCompleted: {
        parseCurve();
        root._initializing = false;
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
            var totalDuration = Math.max(root.animationDuration, 50);
            var progress = Math.min(elapsed / totalDuration, 1);
            var easedProgress = root.evaluateEasing(progress);
            var trackWidth = Math.max(0, boxTrack.animBox.parent.width - root.boxSize);
            boxTrack.animBox.x = Math.max(0, Math.min(trackWidth, easedProgress * trackWidth));
            if (progress >= 1) {
                boxTrack.animBox.x = trackWidth;
                animTimer.stop();
            }
        }
    }

    ColumnLayout {
        id: layout

        anchors.fill: parent
        spacing: 0

        // Interactive curve canvas
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: root.canvasHeight
            radius: Kirigami.Units.smallSpacing * 2
            Kirigami.Theme.colorSet: Kirigami.Theme.View
            Kirigami.Theme.inherit: false
            color: Kirigami.Theme.alternateBackgroundColor
            ToolTip.visible: dragArea.containsMouse
            ToolTip.text: root.isBezier ? i18n("Drag control points to customize the animation curve. Click to replay.") : i18n("Click to replay the animation preview.")
            ToolTip.delay: Kirigami.Units.toolTipDelay

            Canvas {
                id: curveCanvas

                // Control point handles
                function drawHandle(ctx, cx, cy, active) {
                    ctx.beginPath();
                    ctx.arc(cx, cy, root.handleRadius, 0, 2 * Math.PI);
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
                    var pad = root.canvasPad;
                    var gw = w - pad * 2;
                    var gh = h - pad * 2;
                    ctx.clearRect(0, 0, w, h);
                    // Resolve colors fresh each paint so theme context is always current
                    var accentStr = Kirigami.Theme.highlightColor.toString();
                    var gridStr = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.08).toString();
                    var yZero = root.yToCanvas(0, gh);
                    var yOne = root.yToCanvas(1, gh);
                    // Horizontal grid lines at 0.25 increments
                    ctx.strokeStyle = gridStr;
                    ctx.lineWidth = 0.5;
                    ctx.setLineDash([]);
                    for (var gy = 0; gy <= 1; gy += 0.25) {
                        var py = root.yToCanvas(gy, gh);
                        ctx.beginPath();
                        ctx.moveTo(pad, py);
                        ctx.lineTo(pad + gw, py);
                        ctx.stroke();
                    }
                    // Vertical grid lines at 0.2 increments
                    for (var gx = 0; gx <= 1; gx += 0.2) {
                        var px = pad + gx * gw;
                        ctx.beginPath();
                        ctx.moveTo(px, pad);
                        ctx.lineTo(px, pad + gh);
                        ctx.stroke();
                    }
                    // Reference lines at y=0 and y=1 (themed positive color, dashed)
                    var pc = Kirigami.Theme.positiveTextColor;
                    ctx.strokeStyle = Qt.rgba(pc.r, pc.g, pc.b, 0.6).toString();
                    ctx.lineWidth = 1;
                    ctx.setLineDash([6, 4]);
                    ctx.beginPath();
                    ctx.moveTo(pad, yOne);
                    ctx.lineTo(pad + gw, yOne);
                    ctx.stroke();
                    ctx.beginPath();
                    ctx.moveTo(pad, yZero);
                    ctx.lineTo(pad + gw, yZero);
                    ctx.stroke();
                    ctx.setLineDash([]);
                    // Linear diagonal reference (P0 to P3)
                    ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(pad, yZero);
                    ctx.lineTo(pad + gw, yOne);
                    ctx.stroke();
                    if (root.isBezier) {
                        // === BEZIER: control point handles + native bezierCurveTo ===
                        var p0x = pad, p0y = yZero;
                        var p3x = pad + gw, p3y = yOne;
                        var h1x = root.xToCanvas(root.cp1x, gw);
                        var h1y = root.yToCanvas(root.cp1y, gh);
                        var h2x = root.xToCanvas(root.cp2x, gw);
                        var h2y = root.yToCanvas(root.cp2y, gh);
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
                        ctx.strokeStyle = accentStr;
                        ctx.lineWidth = 2;
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
                        ctx.strokeStyle = accentStr;
                        ctx.lineWidth = 2;
                        ctx.beginPath();
                        for (var si = 0; si <= nSamples; si++) {
                            var sx = si / nSamples;
                            var sy = root.evaluateEasing(sx);
                            var px = root.xToCanvas(sx, gw);
                            var py = root.yToCanvas(sy, gh);
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
                    ctx.fillStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.4).toString();
                    ctx.font = Kirigami.Theme.smallFont.pointSize + "pt sans-serif";
                    ctx.textAlign = "right";
                    ctx.fillText("1", pad - 4, yOne + 4);
                    ctx.fillText("0", pad - 4, yZero + 4);
                }
            }

            Connections {
                function onHighlightColorChanged() {
                    curveCanvas.requestPaint();
                }

                function onTextColorChanged() {
                    curveCanvas.requestPaint();
                }

                function onBackgroundColorChanged() {
                    curveCanvas.requestPaint();
                }

                function onPositiveTextColorChanged() {
                    curveCanvas.requestPaint();
                }

                target: Kirigami.Theme
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
                    var gw = curveCanvas.width - root.canvasPad * 2;
                    var gh = curveCanvas.height - root.canvasPad * 2;
                    if (gw <= 0 || gh <= 0)
                        return Infinity;

                    var hx = root.xToCanvas(cpx, gw);
                    var hy = root.yToCanvas(cpy, gh);
                    return Math.sqrt((mx - hx) * (mx - hx) + (my - hy) * (my - hy));
                }

                function nearestHandle(mx, my) {
                    if (!root.isBezier)
                        return 0;

                    var d1 = handleDist(mx, my, root.cp1x, root.cp1y);
                    var d2 = handleDist(mx, my, root.cp2x, root.cp2y);
                    var hitRadius = root.handleHitRadius;
                    if (d1 <= hitRadius && d1 <= d2)
                        return 1;

                    if (d2 <= hitRadius)
                        return 2;

                    return 0;
                }

                Accessible.name: i18n("Drag to adjust easing curve control points")
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true // Crucial: prevents parent ScrollView from stealing vertical drag
                cursorShape: root.isBezier ? (activeHandle ? Qt.ClosedHandCursor : (hoveredHandle ? Qt.OpenHandCursor : Qt.ArrowCursor)) : Qt.ArrowCursor
                onPositionChanged: (mouse) => {
                    if (activeHandle) {
                        // Start drag immediately or with very small threshold for reliability
                        if (!dragStarted) {
                            var dist = Math.sqrt(Math.pow(mouse.x - pressPos.x, 2) + Math.pow(mouse.y - pressPos.y, 2));
                            if (dist > 2)
                                dragStarted = true;

                        }
                        if (dragStarted) {
                            var gw = curveCanvas.width - root.canvasPad * 2;
                            var gh = curveCanvas.height - root.canvasPad * 2;
                            if (gw <= 0 || gh <= 0)
                                return ;

                            var nx = Math.max(0, Math.min(1, root.canvasToX(mouse.x, gw)));
                            var ny = Math.max(root.yMin, Math.min(root.yMax, root.canvasToY(mouse.y, gh)));
                            // High-precision update during drag for smoothness
                            if (activeHandle === 1) {
                                root.cp1x = nx;
                                root.cp1y = ny;
                            } else {
                                root.cp2x = nx;
                                root.cp2y = ny;
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
                        root.cp1x = Math.round(root.cp1x * 100) / 100;
                        root.cp1y = Math.round(root.cp1y * 100) / 100;
                        root.cp2x = Math.round(root.cp2x * 100) / 100;
                        root.cp2y = Math.round(root.cp2y * 100) / 100;
                        activeHandle = 0;
                        root.replay();
                        // Emit the update only once at the end of the drag to ensure stability
                        root.curveEdited(root.formatCurve());
                    }
                }
                onCanceled: {
                    activeHandle = 0;
                    dragStarted = false;
                }
                onClicked: {
                    // Replay on click if it wasn't a significant drag
                    if (!dragStarted)
                        root.replay();

                }
            }

        }

        // Curve value display
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: root.canvasPad
            Layout.rightMargin: root.canvasPad
            text: root.curveLabel()
            font: Kirigami.Theme.fixedWidthFont
            color: Kirigami.Theme.disabledTextColor
            horizontalAlignment: Text.AlignHCenter
        }

        // Animated box track
        AnimatedBoxTrack {
            id: boxTrack

            Layout.fillWidth: true
            Layout.preferredHeight: root.boxTrackHeight
            boxSize: root.boxSize
        }

    }

}
