// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

.pragma library

// .pragma library directly under the SPDX header: qt6_target_qml_sources
// only reads the first 128 bytes when it decides whether the script is
// shared.

// Phosphor.Bar.DropProxyGeometry, the placement map's drop-proxy rects.
//
// The bar registers its miniature with the daemon as a drop proxy for the
// compositor's own window drag (A2 §1.5, WindowDrag.registerDropProxy):
// the miniature's rect in screen pixels plus one rect per zone cell, so a
// drag ending over a cell of the miniature commits to the real zone. The
// cell rects are the model's 0..1 fractions scaled onto the miniature.
// Pure so the bar test can check the mapping without a daemon.

// `miniRect` is {x, y, width, height} in screen pixels; `cells` the model's
// cell list. Returns one {id, x, y, w, h} per cell with a non-empty id and
// a non-empty rect, rounded to whole pixels.
function cellRects(miniRect, cells) {
    var out = [];
    if (!miniRect || !(miniRect.width > 0) || !(miniRect.height > 0))
        return out;
    for (var i = 0; i < (cells ? cells.length : 0); ++i) {
        var c = cells[i];
        var id = c && c.id !== undefined && c.id !== null ? String(c.id) : "";
        if (id === "")
            continue;
        var x0 = Math.round(miniRect.x + Number(c.x) * miniRect.width);
        var y0 = Math.round(miniRect.y + Number(c.y) * miniRect.height);
        var x1 = Math.round(miniRect.x + (Number(c.x) + Number(c.w)) * miniRect.width);
        var y1 = Math.round(miniRect.y + (Number(c.y) + Number(c.h)) * miniRect.height);
        if (!(x1 > x0) || !(y1 > y0))
            continue;
        out.push({ id: id, x: x0, y: y0, w: x1 - x0, h: y1 - y0 });
    }
    return out;
}
