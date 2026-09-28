// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Rectangle {
    id: root
    color: Appearance.recess
    Accessible.ignored: true
    // Abstract panes deliberately carry no window geometry, icons or titles.
    Canvas {
        id: atmosphere
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: Appearance
            function onStopsChanged() {
                atmosphere.requestPaint();
            }
            function onOutlineChanged() {
                atmosphere.requestPaint();
            }
        }
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            const fields = [[.2, .4, .52, 0, .12], [.72, .28, .48, 2, .18], [.65, 1.1, .6, 1, .24]];
            for (const f of fields) {
                ctx.save();
                ctx.scale(width, height);
                const g = ctx.createRadialGradient(f[0], f[1], 0, f[0], f[1], f[2]);
                g.addColorStop(0, Qt.alpha(Appearance.stops[f[3]], f[4]));
                g.addColorStop(1, "transparent");
                ctx.fillStyle = g;
                ctx.fillRect(0, 0, 1, 1);
                ctx.restore();
            }
            ctx.save();
            ctx.translate(width * .51, height * 1.13);
            ctx.rotate(-13 * Math.PI / 180);
            ctx.scale(width / 1440, height / 900);
            ctx.beginPath();
            ctx.ellipse(-800, -350, 1600, 700);
            ctx.fillStyle = Qt.alpha(Appearance.stops[1], .04);
            ctx.fill();
            ctx.strokeStyle = Appearance.outline;
            ctx.lineWidth = 1;
            ctx.stroke();
            ctx.restore();
        }
    }
    Item {
        x: root.width * .05
        y: root.height / 6
        width: root.width * .9
        height: root.height * .6333
        rotation: -5
        Repeater {
            model: [Qt.rect(0, 0, .57, 1), Qt.rect(.585, 0, .415, .44), Qt.rect(.585, .47, .415, .53)]
            Rectangle {
                required property rect modelData
                required property int index
                readonly property color tint: Appearance.windowColor(index)
                x: modelData.x * parent.width
                y: modelData.y * parent.height
                width: modelData.width * parent.width
                height: modelData.height * parent.height
                radius: Appearance.radius * 2
                border.width: 1
                border.color: Qt.alpha(tint, .22)
                gradient: Gradient {
                    GradientStop {
                        position: 0
                        color: Qt.alpha(tint, .05)
                    }
                    GradientStop {
                        position: .65
                        color: "transparent"
                    }
                }
            }
        }
    }
}
