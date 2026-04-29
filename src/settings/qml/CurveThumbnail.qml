// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "EasingCurve.js" as EC
import "SpringPhysics.js" as SP

Rectangle {
    id: root

    property string curve: "0.33,1.00,0.68,1.00"
    property int timingMode: 0
    property real springOmega: 12.0
    property real springZeta: 0.8

    implicitWidth: 80
    implicitHeight: 50
    radius: 4
    color: Kirigami.Theme.backgroundColor
    border.color: hoverHandler.hovered ? Kirigami.Theme.highlightColor : Kirigami.Theme.separatorColor
    border.width: 1

    Canvas {
        id: canvas

        anchors.fill: parent
        anchors.margins: 4

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.3);
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(0, height);
            ctx.lineTo(width, 0);
            ctx.stroke();

            ctx.strokeStyle = Kirigami.Theme.highlightColor;
            ctx.lineWidth = 1.5;
            ctx.beginPath();
            var samples = 60;
            for (var i = 0; i <= samples; i++) {
                var t = i / samples;
                var v;
                if (root.timingMode === 0) {
                    v = EC.evaluate(t, root.curve);
                } else {
                    var settleTime = SP.estimateSettleTime(root.springOmega, root.springZeta, 0.01);
                    v = SP.evaluate(t * settleTime, root.springOmega, root.springZeta);
                }
                var x = t * width;
                var y = height - v * height;
                y = Math.max(0, Math.min(height, y));
                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.stroke();
        }
    }

    HoverHandler {
        id: hoverHandler
    }

    onCurveChanged: canvas.requestPaint()
    onTimingModeChanged: canvas.requestPaint()
    onSpringOmegaChanged: canvas.requestPaint()
    onSpringZetaChanged: canvas.requestPaint()

    Component.onCompleted: canvas.requestPaint()
}
