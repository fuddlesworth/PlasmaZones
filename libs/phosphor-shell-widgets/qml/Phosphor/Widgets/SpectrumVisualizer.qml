// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

Canvas {
    id: root
    property bool playing: false
    property var spectrum: AudioSpectrum
    property string style: Appearance.visualizer
    readonly property bool analyzing: playing && visible && Window.window !== null && Window.window.visible && Appearance.motion && style !== "off"
    implicitWidth: 280
    implicitHeight: 90
    property real phase: 0
    FrameAnimation {
        running: root.analyzing
        onTriggered: {
            root.phase += frameTime;
            root.requestPaint();
        }
    }
    Accessible.ignored: true
    property var registeredSpectrum: null
    function updateAnalyzer() {
        if (registeredSpectrum && registeredSpectrum !== spectrum)
            registeredSpectrum.setActive(root, false);
        registeredSpectrum = spectrum;
        if (spectrum)
            spectrum.setActive(root, analyzing);
        requestPaint();
    }
    onSpectrumChanged: updateAnalyzer()
    onAnalyzingChanged: updateAnalyzer()
    Component.onCompleted: updateAnalyzer()
    Component.onDestruction: if (registeredSpectrum)
        registeredSpectrum.setActive(root, false)
    onStyleChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    Connections {
        target: root.spectrum
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
        const values = analyzing && spectrum ? spectrum.samples : [];
        const sample = position => {
            if (!values.length)
                return 0;
            const at = Math.max(0, Math.min(values.length - 1, position * (values.length - 1)));
            const index = Math.floor(at);
            return values[index] * (1 - at + index) + values[Math.min(index + 1, values.length - 1)] * (at - index);
        };
        const gradient = ctx.createLinearGradient(0, 0, width, 0);
        for (let i = 0; i < 4; ++i)
            gradient.addColorStop(i / 3, Appearance.stops[i]);
        ctx.fillStyle = gradient;
        ctx.strokeStyle = gradient;
        ctx.lineCap = "round";
        ctx.lineJoin = "round";
        ctx.shadowColor = Appearance.accent;
        ctx.shadowBlur = Appearance.glow ? 4 : 0;
        if (style === "bars") {
            const count = width < 60 ? 10 : 42;
            const step = width / count;
            for (let i = 0; i < count; ++i) {
                const h = Math.max(1.5, sample(i / (count - 1)) * height * 0.78);
                ctx.globalAlpha = 0.85;
                ctx.fillRect(i * step, height * 0.82 - h, Math.max(1, step - 1.5), h);
                if (width >= 60) {
                    ctx.globalAlpha = 0.13;
                    ctx.fillRect(i * step, height * 0.85, Math.max(1, step - 1.5), Math.min(h * 0.2, height * 0.14));
                }
            }
        } else if (style === "halo") {
            const r = height * 0.25, cx = width / 2, cy = height / 2;
            ctx.lineWidth = 1;
            ctx.globalAlpha = 0.75;
            for (let i = 0; i < 72; ++i) {
                const a = i * Math.PI / 36 - Math.PI / 2;
                const length = 1.5 + sample(Math.abs(36 - i) / 36) * height * 0.28;
                ctx.beginPath();
                ctx.moveTo(cx + Math.cos(a) * r, cy + Math.sin(a) * r);
                ctx.lineTo(cx + Math.cos(a) * (r + length), cy + Math.sin(a) * (r + length));
                ctx.stroke();
            }
            ctx.globalAlpha = 0.28;
            ctx.beginPath();
            ctx.arc(cx, cy, r - 3, 0, 2 * Math.PI);
            ctx.stroke();
            ctx.globalAlpha = 0.8;
            ctx.shadowBlur = 0;
            ctx.beginPath();
            for (let i = 0; i <= 40; ++i) {
                const t = i / 40;
                const x = cx + (t - 0.5) * r * 1.24;
                const y = cy + Math.sin(t * 14 - phase * 3) * Math.sin(t * Math.PI) * sample(t) * height * 0.09;
                if (i === 0)
                    ctx.moveTo(x, y);
                else
                    ctx.lineTo(x, y);
            }
            ctx.stroke();
            ctx.globalAlpha = 0.18;
            ctx.beginPath();
            ctx.moveTo(5, cy);
            ctx.lineTo(cx - r - height * 0.22, cy);
            ctx.moveTo(cx + r + height * 0.22, cy);
            ctx.lineTo(width - 5, cy);
            ctx.stroke();
        } else {
            for (let layer = 3; layer >= 0; --layer) {
                ctx.globalAlpha = layer === 0 ? 0.95 : 0.14 + layer * 0.055;
                ctx.lineWidth = layer === 0 ? 1 : 0.65;
                ctx.beginPath();
                for (let i = 0; i <= 120; ++i) {
                    const t = i / 120;
                    const taper = Math.pow(Math.sin(t * Math.PI), 0.65);
                    const amplitude = sample(Math.max(0, t - layer * 0.012)) * height * 0.52 * taper;
                    const y = height * 0.5 + Math.sin(t * 13 - phase * 2 + layer * 0.56) * amplitude;
                    if (i === 0)
                        ctx.moveTo(0, y);
                    else
                        ctx.lineTo(t * width, y);
                }
                ctx.stroke();
                if (layer === 0) {
                    ctx.lineTo(width, height * 0.86);
                    ctx.lineTo(0, height * 0.86);
                    ctx.closePath();
                    ctx.globalAlpha = 0.055;
                    ctx.fill();
                }
            }
        }
        ctx.globalAlpha = 1;
        ctx.shadowBlur = 0;
    }
}
