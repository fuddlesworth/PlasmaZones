// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

/**
 * @brief Cubic bezier curve editor with draggable control points
 *
 * Provides an interactive canvas for editing a cubic bezier easing curve
 * defined by two control points P1 and P2, plus an animated preview box
 * that demonstrates the curve. Click the canvas to replay the animation.
 */
Item {
    id: root

    Accessible.name: i18n("Animation easing curve editor")
    Accessible.description: i18n("Drag the control points to customize the animation curve. Click to replay the preview.")

    property string curve: root.defaultCurve
    property int animationDuration: 150
    property bool previewEnabled: true

    signal curveEdited(string newCurve)

    implicitHeight: layout.implicitHeight
    implicitWidth: 400

    readonly property int canvasHeight: 240
    readonly property int boxTrackHeight: 36
    readonly property int boxSize: 22
    readonly property int handleRadius: 10
    readonly property int canvasPad: 24
    readonly property int handleHitRadius: 18

    // Y range: [-1, 2] mapped to canvas, allowing overshoot
    readonly property real yMin: -1.0
    readonly property real yMax: 2.0

    // Default curve string (Ease Out Cubic) — matches plasmazones.kcfg
    readonly property string defaultCurve: "0.33,1.00,0.68,1.00"

    // Parsed control point values (internal state)
    property real cp1x: 0.33
    property real cp1y: 1.00
    property real cp2x: 0.68
    property real cp2y: 1.00

    // Parse the curve string into control point properties; reset to default on failure
    function parseCurve() {
        var parts = root.curve.split(",")
        if (parts.length === 4) {
            var x1 = parseFloat(parts[0])
            var y1 = parseFloat(parts[1])
            var x2 = parseFloat(parts[2])
            var y2 = parseFloat(parts[3])
            if (isFinite(x1) && isFinite(y1) && isFinite(x2) && isFinite(y2)) {
                cp1x = x1; cp1y = y1; cp2x = x2; cp2y = y2
                return
            }
        }
        // Reset to default on malformed input; emit to fix stored config
        var def = root.defaultCurve.split(",")
        if (def.length === 4) {
            cp1x = parseFloat(def[0]); cp1y = parseFloat(def[1])
            cp2x = parseFloat(def[2]); cp2y = parseFloat(def[3])
        }
        root.curveEdited(root.formatCurve())
    }

    // Format control point values back into a curve string
    function formatCurve() {
        return cp1x.toFixed(2) + "," + cp1y.toFixed(2) + ","
             + cp2x.toFixed(2) + "," + cp2y.toFixed(2)
    }

    // Cubic bezier Y at parameter t
    function cubicBezier(y1, y2, t) {
        var mt = 1 - t
        return 3 * mt * mt * t * y1 + 3 * mt * t * t * y2 + t * t * t
    }

    // Newton's method: find parameter t where Bx(t) = x
    function solveBezierX(x1, x2, x) {
        var t = x
        for (var i = 0; i < 8; i++) {
            var mt = 1 - t
            var bx = 3 * mt * mt * t * x1 + 3 * mt * t * t * x2 + t * t * t - x
            var dbx = 3 * mt * mt * x1 + 6 * mt * t * (x2 - x1) + 3 * t * t * (1 - x2)
            if (Math.abs(dbx) < 1e-12) break
            t -= bx / dbx
            t = Math.max(0, Math.min(1, t))
        }
        return t
    }

    // Evaluate the easing for a given progress x in [0,1]
    function evaluateEasing(x) {
        if (x <= 0) return 0
        if (x >= 1) return 1
        var t = solveBezierX(cp1x, cp2x, x)
        return cubicBezier(cp1y, cp2y, t)
    }

    // Map a curve Y value to canvas pixel Y
    function yToCanvas(v, gh) {
        return canvasPad + (1.0 - (v - yMin) / (yMax - yMin)) * gh
    }

    // Map canvas pixel Y back to curve Y value
    function canvasToY(py, gh) {
        if (gh <= 0) return 0.5
        return yMax - (py - canvasPad) / gh * (yMax - yMin)
    }

    // Map a curve X value [0,1] to canvas pixel X
    function xToCanvas(v, gw) {
        return canvasPad + v * gw
    }

    // Map canvas pixel X back to curve X value
    function canvasToX(px, gw) {
        if (gw <= 0) return 0.5
        return (px - canvasPad) / gw
    }

    onCurveChanged: {
        if (!dragArea.activeHandle) {
            parseCurve()
            curveCanvas.requestPaint()
            replay()
        }
    }

    Component.onCompleted: {
        parseCurve()
        curveCanvas.requestPaint()
        Qt.callLater(replay)
    }

    function replay() {
        if (!root.previewEnabled) return
        animTimer.stop()
        animBox.x = 0
        curveCanvas.requestPaint()
        replayDelay.restart()
    }

    Timer {
        id: replayDelay
        interval: 80
        onTriggered: {
            animTimer.elapsed = 0
            animTimer.lastTime = Date.now()
            animTimer.start()
        }
    }

    Timer {
        id: animTimer
        interval: 16  // ~60fps
        repeat: true
        property real elapsed: 0
        property real lastTime: 0

        onTriggered: {
            var now = Date.now()
            elapsed += (now - lastTime)
            lastTime = now

            var totalDuration = Math.max(root.animationDuration, 50)
            var progress = Math.min(elapsed / totalDuration, 1.0)
            var easedProgress = root.evaluateEasing(progress)

            var trackWidth = Math.max(0, animBox.parent.width - root.boxSize)
            animBox.x = easedProgress * trackWidth

            if (progress >= 1.0) {
                animBox.x = trackWidth
                animTimer.stop()
            }
        }
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        spacing: 0

        // Interactive bezier curve canvas
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.canvasHeight

            ToolTip.visible: dragArea.containsMouse
            ToolTip.text: i18n("Drag control points to customize the animation curve. Click to replay.")
            ToolTip.delay: 500

            Canvas {
                id: curveCanvas
                anchors.fill: parent

                onPaint: {
                    var ctx = getContext("2d")
                    var w = width, h = height
                    var pad = root.canvasPad
                    var gw = w - pad * 2
                    var gh = h - pad * 2

                    ctx.clearRect(0, 0, w, h)

                    // Subtle background fill
                    ctx.fillStyle = Qt.rgba(Kirigami.Theme.backgroundColor.r,
                                            Kirigami.Theme.backgroundColor.g,
                                            Kirigami.Theme.backgroundColor.b, 0.4)
                    ctx.fillRect(0, 0, w, h)

                    // Reference lines at y=0 and y=1
                    var refColor = Qt.rgba(Kirigami.Theme.disabledTextColor.r,
                                           Kirigami.Theme.disabledTextColor.g,
                                           Kirigami.Theme.disabledTextColor.b, 0.25)
                    ctx.strokeStyle = refColor
                    ctx.lineWidth = 1
                    ctx.setLineDash([])
                    var yZero = root.yToCanvas(0, gh)
                    var yOne = root.yToCanvas(1, gh)
                    ctx.beginPath(); ctx.moveTo(pad, yZero); ctx.lineTo(pad + gw, yZero); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(pad, yOne); ctx.lineTo(pad + gw, yOne); ctx.stroke()

                    // Grid lines (vertical, dashed)
                    ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r,
                                              Kirigami.Theme.disabledTextColor.g,
                                              Kirigami.Theme.disabledTextColor.b, 0.1)
                    ctx.setLineDash([2, 4])
                    for (var j = 0; j <= 4; j++) {
                        var gx = pad + gw * j / 4
                        ctx.beginPath(); ctx.moveTo(gx, pad); ctx.lineTo(gx, pad + gh); ctx.stroke()
                    }
                    ctx.setLineDash([])

                    // Linear diagonal reference (P0 to P3)
                    ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r,
                                              Kirigami.Theme.disabledTextColor.g,
                                              Kirigami.Theme.disabledTextColor.b, 0.2)
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(pad, yZero)
                    ctx.lineTo(pad + gw, yOne)
                    ctx.stroke()

                    // Control point positions in canvas coords
                    var p0x = pad, p0y = yZero
                    var p3x = pad + gw, p3y = yOne
                    var h1x = root.xToCanvas(root.cp1x, gw)
                    var h1y = root.yToCanvas(root.cp1y, gh)
                    var h2x = root.xToCanvas(root.cp2x, gw)
                    var h2y = root.yToCanvas(root.cp2y, gh)

                    // Handle connection lines (dashed): P0->P1 and P3->P2
                    ctx.strokeStyle = Qt.rgba(Kirigami.Theme.highlightColor.r,
                                              Kirigami.Theme.highlightColor.g,
                                              Kirigami.Theme.highlightColor.b, 0.5)
                    ctx.lineWidth = 1.5
                    ctx.setLineDash([4, 3])
                    ctx.beginPath(); ctx.moveTo(p0x, p0y); ctx.lineTo(h1x, h1y); ctx.stroke()
                    ctx.beginPath(); ctx.moveTo(p3x, p3y); ctx.lineTo(h2x, h2y); ctx.stroke()
                    ctx.setLineDash([])

                    // Cubic bezier curve (highlighted)
                    ctx.strokeStyle = Kirigami.Theme.highlightColor
                    ctx.lineWidth = 2.5
                    ctx.beginPath()
                    ctx.moveTo(p0x, p0y)
                    ctx.bezierCurveTo(h1x, h1y, h2x, h2y, p3x, p3y)
                    ctx.stroke()

                    // Control point handles (inactive = dimmer for visual feedback)
                    function drawHandle(cx, cy, active) {
                        ctx.beginPath()
                        ctx.arc(cx, cy, root.handleRadius, 0, 2 * Math.PI)
                        ctx.fillStyle = active ? Kirigami.Theme.highlightColor
                            : Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                                      Kirigami.Theme.highlightColor.b, 0.6)
                        ctx.fill()
                        ctx.strokeStyle = Qt.rgba(Kirigami.Theme.highlightedTextColor.r,
                                                  Kirigami.Theme.highlightedTextColor.g,
                                                  Kirigami.Theme.highlightedTextColor.b, 0.8)
                        ctx.lineWidth = active ? 2.5 : 1.5
                        ctx.stroke()
                    }

                    drawHandle(h1x, h1y, dragArea.activeHandle === 1)
                    drawHandle(h2x, h2y, dragArea.activeHandle === 2)

                    // Anchor dots at P0 and P3
                    ctx.fillStyle = Kirigami.Theme.disabledTextColor
                    ctx.beginPath(); ctx.arc(p0x, p0y, 3, 0, 2 * Math.PI); ctx.fill()
                    ctx.beginPath(); ctx.arc(p3x, p3y, 3, 0, 2 * Math.PI); ctx.fill()

                    // Axis labels
                    ctx.fillStyle = Qt.rgba(Kirigami.Theme.textColor.r,
                                            Kirigami.Theme.textColor.g,
                                            Kirigami.Theme.textColor.b, 0.4)
                    ctx.font = "9px sans-serif"
                    ctx.textAlign = "right"
                    ctx.fillText("0", pad - 3, yZero + 3)
                    ctx.fillText("1", pad - 3, yOne + 3)
                }
            }

            // Single MouseArea for all handle interaction
            MouseArea {
                id: dragArea
                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true // Crucial: prevents parent ScrollView from stealing vertical drag
                cursorShape: activeHandle ? Qt.ClosedHandCursor
                           : (hoveredHandle ? Qt.OpenHandCursor : Qt.ArrowCursor)

                // 0 = none, 1 = P1, 2 = P2
                property int activeHandle: 0
                property int hoveredHandle: 0
                property bool dragStarted: false
                property point pressPos: Qt.point(0, 0)

                function handleDist(mx, my, cpx, cpy) {
                    var gw = curveCanvas.width - root.canvasPad * 2
                    var gh = curveCanvas.height - root.canvasPad * 2
                    if (gw <= 0 || gh <= 0) return Infinity
                    var hx = root.xToCanvas(cpx, gw)
                    var hy = root.yToCanvas(cpy, gh)
                    return Math.sqrt((mx - hx) * (mx - hx) + (my - hy) * (my - hy))
                }

                function nearestHandle(mx, my) {
                    var d1 = handleDist(mx, my, root.cp1x, root.cp1y)
                    var d2 = handleDist(mx, my, root.cp2x, root.cp2y)
                    var hitRadius = root.handleHitRadius
                    if (d1 <= hitRadius && d1 <= d2) return 1
                    if (d2 <= hitRadius) return 2
                    return 0
                }

                onPositionChanged: (mouse) => {
                    if (activeHandle) {
                        // Start drag immediately or with very small threshold for reliability
                        if (!dragStarted) {
                            var dist = Math.sqrt(Math.pow(mouse.x - pressPos.x, 2) + Math.pow(mouse.y - pressPos.y, 2))
                            if (dist > 2) {
                                dragStarted = true
                            }
                        }

                        if (dragStarted) {
                            var gw = curveCanvas.width - root.canvasPad * 2
                            var gh = curveCanvas.height - root.canvasPad * 2
                            if (gw <= 0 || gh <= 0) return
                            var nx = Math.max(0, Math.min(1, root.canvasToX(mouse.x, gw)))
                            var ny = Math.max(root.yMin, Math.min(root.yMax, root.canvasToY(mouse.y, gh)))
                            
                            // High-precision update during drag for smoothness
                            if (activeHandle === 1) {
                                root.cp1x = nx; root.cp1y = ny
                            } else {
                                root.cp2x = nx; root.cp2y = ny
                            }
                            curveCanvas.requestPaint()
                            // NOTE: We DON'T call curveEdited here to avoid Hammering config/backend during drag
                        }
                    } else {
                        hoveredHandle = nearestHandle(mouse.x, mouse.y)
                    }
                }

                onPressed: (mouse) => {
                    pressPos = Qt.point(mouse.x, mouse.y)
                    dragStarted = false
                    activeHandle = nearestHandle(mouse.x, mouse.y)
                }

                onReleased: {
                    if (activeHandle) {
                        // Snap to 0.01 increments only on release for clean config values
                        root.cp1x = Math.round(root.cp1x * 100) / 100
                        root.cp1y = Math.round(root.cp1y * 100) / 100
                        root.cp2x = Math.round(root.cp2x * 100) / 100
                        root.cp2y = Math.round(root.cp2y * 100) / 100
                        
                        activeHandle = 0
                        root.replay()
                        // Emit the update only once at the end of the drag to ensure stability
                        root.curveEdited(root.formatCurve())
                    }
                }

                onCanceled: {
                    activeHandle = 0
                    dragStarted = false
                }

                onClicked: {
                    // Replay on click if it wasn't a significant drag
                    if (!dragStarted) {
                        root.replay()
                    }
                }
            }
        }

        // Bezier value display
        Label {
            Layout.fillWidth: true
            Layout.leftMargin: root.canvasPad
            Layout.rightMargin: root.canvasPad
            text: i18n("cubic-bezier(%1, %2, %3, %4)",
                root.cp1x.toFixed(2), root.cp1y.toFixed(2), root.cp2x.toFixed(2), root.cp2y.toFixed(2))
            font.family: "monospace"
            font.pointSize: Kirigami.Theme.smallFont.pointSize
            color: Kirigami.Theme.disabledTextColor
            horizontalAlignment: Text.AlignHCenter
        }

        // Animated box track
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: root.boxTrackHeight

            // Track rail
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: root.boxSize / 2
                anchors.rightMargin: root.boxSize / 2
                height: 3
                radius: 1.5
                color: Kirigami.Theme.separatorColor
            }

            // Start marker
            Rectangle {
                x: root.boxSize / 2 - 1
                anchors.verticalCenter: parent.verticalCenter
                width: 2; height: 12; radius: 1
                color: Kirigami.Theme.disabledTextColor
                opacity: 0.4
            }

            // End marker
            Rectangle {
                x: parent.width - root.boxSize / 2 - 1
                anchors.verticalCenter: parent.verticalCenter
                width: 2; height: 12; radius: 1
                color: Kirigami.Theme.disabledTextColor
                opacity: 0.4
            }

            // Animated box
            Rectangle {
                id: animBox
                width: root.boxSize
                height: root.boxSize
                radius: root.boxSize / 5
                y: (parent.height - height) / 2
                x: 0
                color: Kirigami.Theme.highlightColor
            }
        }
    }
}
