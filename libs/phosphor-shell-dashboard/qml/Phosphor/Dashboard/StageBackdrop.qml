// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
import QtQuick
import Phosphor.Theme

// Cut out the actual window silhouettes, so the space between previews stays
// part of the dimmed desktop. Destination-out also handles overlapping windows.
Canvas {
    id: root
    property rect bar
    property rect preview
    property var windows: []
    property real radius: Appearance.radius
    property color tint: Qt.alpha(Appearance.recess, 0.76)
    onBarChanged: requestPaint()
    onPreviewChanged: requestPaint()
    onWindowsChanged: requestPaint()
    onRadiusChanged: requestPaint()
    onTintChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d");
        ctx.reset();
        ctx.fillStyle = tint;
        ctx.fillRect(0, 0, width, height);
        ctx.globalCompositeOperation = "destination-out";
        ctx.fillStyle = "black";
        function cutout(rect, radius) {
            const x = rect.x, y = rect.y, w = rect.width, h = rect.height;
            const r = Math.min(radius, w / 2, h / 2);
            if (w <= 0 || h <= 0)
                return;
            ctx.beginPath();
            ctx.moveTo(x + r, y);
            ctx.lineTo(x + w - r, y);
            ctx.quadraticCurveTo(x + w, y, x + w, y + r);
            ctx.lineTo(x + w, y + h - r);
            ctx.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
            ctx.lineTo(x + r, y + h);
            ctx.quadraticCurveTo(x, y + h, x, y + h - r);
            ctx.lineTo(x, y + r);
            ctx.quadraticCurveTo(x, y, x + r, y);
            ctx.closePath();
            ctx.fill();
        }
        cutout(bar, radius);
        ctx.beginPath();
        ctx.rect(preview.x, preview.y, preview.width, preview.height);
        ctx.clip();
        for (const window of windows)
            cutout(window, radius);
    }
}
