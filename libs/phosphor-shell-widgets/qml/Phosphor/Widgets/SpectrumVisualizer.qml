// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Canvas {
    id: root
    property bool playing: false
    property string style: Appearance.visualizer
    readonly property bool analyzing: playing && visible && Window.window !== null && Window.window.visible && Appearance.motion && Appearance.media && style !== "off"
    implicitWidth: 280
    implicitHeight: 72
    Accessible.ignored: true
    onAnalyzingChanged: AudioSpectrum.setActive(root, analyzing)
    Component.onCompleted: AudioSpectrum.setActive(root, analyzing)
    Component.onDestruction: AudioSpectrum.setActive(root, false)
    onStyleChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Connections {
        target: AudioSpectrum
        function onSamplesChanged(): void {
            if (root.visible)
                root.requestPaint();
        }
    }
    Connections {
        target: Appearance
        function onStopsChanged(): void {
            root.requestPaint();
        }
    }
    onPaint: {
        const ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);
        if (style === "off")
            return;
        const values = analyzing ? AudioSpectrum.samples : [];
        const count = Math.max(24, values.length);
        const gradient = ctx.createLinearGradient(0, 0, width, 0);
        for (let i = 0; i < 4; ++i)
            gradient.addColorStop(i / 3, Appearance.stops[i]);
        ctx.fillStyle = gradient;
        ctx.strokeStyle = gradient;
        ctx.lineWidth = 2;
        if (style === "bars") {
            const step = width / count;
            for (let i = 0; i < count; ++i) {
                const h = Math.max(2, (values[i] || 0) * (height - 4));
                ctx.fillRect(i * step + 1, height - h, Math.max(1, step - 3), h);
            }
        } else if (style === "halo") {
            const radius = Math.min(width, height) * 0.3;
            ctx.beginPath();
            for (let i = 0; i <= count; ++i) {
                const theta = (i / count) * Math.PI * 2;
                const r = radius + (values[i % count] || 0) * height * 0.17;
                const x = width / 2 + Math.cos(theta) * r;
                const y = height / 2 + Math.sin(theta) * r;
                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.closePath();
            ctx.stroke();
        } else {
            for (let layer = 0; layer < 3; ++layer) {
                ctx.globalAlpha = 1 - layer * 0.28;
                ctx.beginPath();
                for (let i = 0; i < count; ++i) {
                    const x = i / (count - 1) * width;
                    const amplitude = (values[i] || 0) * height * (0.65 - layer * 0.14);
                    const y = height * 0.82 - amplitude - layer * 3;
                    if (i === 0)
                        ctx.moveTo(x, y);
                    else
                        ctx.lineTo(x, y);
                }
                ctx.stroke();
            }
        }
        ctx.globalAlpha = 1;
    }
}
