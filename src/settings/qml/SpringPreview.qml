// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import "SpringPhysics.js" as SP

ColumnLayout {
    id: root

    property real omega: 12.0
    property real zeta: 0.8
    property bool previewEnabled: true

    spacing: Kirigami.Units.smallSpacing

    Canvas {
        id: graphCanvas

        Layout.fillWidth: true
        Layout.preferredHeight: 120

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);

            var margin = 8;
            var gw = width - 2 * margin;
            var gh = height - 2 * margin;

            ctx.strokeStyle = Qt.rgba(Kirigami.Theme.disabledTextColor.r, Kirigami.Theme.disabledTextColor.g, Kirigami.Theme.disabledTextColor.b, 0.2);
            ctx.lineWidth = 0.5;
            ctx.beginPath();
            ctx.moveTo(margin, margin + gh * 0.5);
            ctx.lineTo(margin + gw, margin + gh * 0.5);
            ctx.stroke();
            ctx.beginPath();
            ctx.moveTo(margin, margin);
            ctx.lineTo(margin + gw, margin);
            ctx.stroke();

            var settleTime = SP.estimateSettleTime(root.omega, root.zeta, 0.01);
            ctx.strokeStyle = Kirigami.Theme.highlightColor;
            ctx.lineWidth = 2;
            ctx.beginPath();
            var samples = 120;
            for (var i = 0; i <= samples; i++) {
                var t = (i / samples) * settleTime;
                var v = SP.evaluate(t, root.omega, root.zeta);
                var x = margin + (i / samples) * gw;
                var y = margin + (1.0 - v) * gh;
                y = Math.max(margin, Math.min(margin + gh, y));
                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.stroke();

            if (root.zeta < 1.0) {
                ctx.strokeStyle = Qt.rgba(Kirigami.Theme.negativeTextColor.r, Kirigami.Theme.negativeTextColor.g, Kirigami.Theme.negativeTextColor.b, 0.3);
                ctx.lineWidth = 1;
                ctx.setLineDash([4, 4]);
                ctx.beginPath();
                for (var j = 0; j <= samples; j++) {
                    var t2 = (j / samples) * settleTime;
                    var decay = Math.exp(-root.zeta * root.omega * t2);
                    var env = 1.0 + decay;
                    var x2 = margin + (j / samples) * gw;
                    var y2 = margin + (1.0 - env) * gh;
                    y2 = Math.max(margin, Math.min(margin + gh, y2));
                    if (j === 0)
                        ctx.moveTo(x2, y2);
                    else
                        ctx.lineTo(x2, y2);
                }
                ctx.stroke();
                ctx.setLineDash([]);
            }
        }
    }

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        font: Kirigami.Theme.smallFont
        color: Kirigami.Theme.disabledTextColor
        text: {
            var settleTime = SP.estimateSettleTime(root.omega, root.zeta, 0.01);
            var label = root.zeta < 1.0 ? i18n("Underdamped (bouncy)") : root.zeta > 1.0 ? i18n("Overdamped (slow)") : i18n("Critically damped");
            return label + " — " + i18n("%1 ms settle", Math.round(settleTime * 1000));
        }
    }

    onOmegaChanged: graphCanvas.requestPaint()
    onZetaChanged: graphCanvas.requestPaint()
    Component.onCompleted: graphCanvas.requestPaint()
}
