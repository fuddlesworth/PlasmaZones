// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
// Phosphor.Widgets.ConnectorGeometry, connected-corner path math.
//
// Pure geometry: builds the SVG path `d` string for the connected-corner
// bar shape, where the bar's bottom edge weaves down into "sockets"
// (pockets) that attached popouts fill, so the bar and its popouts read
// as one continuous painted surface. Consumed by ConnectedShape /
// BarCanvas via a PathSvg element, which parses the same SVG path grammar
// the design mockups (docs/phosphor-shell-design/mockups/*.svg) are drawn
// in, so the rendered shape matches the mockups command-for-command.
//
// .pragma library: stateless math, shared as one instance across every
// importer. No QML context access, no per-component state.
.pragma library

// Round to 2 decimals so the generated path strings stay compact and
// free of float noise (1.9999999 -> 2). Sub-pixel precision past this is
// invisible and only bloats the string + defeats string-equality tests.
function _n(v) {
    return Math.round(v * 100) / 100;
}

// Build the connected-corner bar path.
//
//   w, h            bar strip size (h is the closed bar height)
//   cornerRadius    radius of the bar's own four outer corners
//   connectorRadius radius of each socket's fillets (convex pocket-bottom
//                   corners + concave/inverted top corners)
//   sockets         array of { x, width, depth }, each a downward pocket:
//                     x      left wall, in bar-local coordinates
//                     width  wall-to-wall span
//                     depth  downward extent below the bar's bottom edge
//
// Sockets with depth <= MIN_DEPTH render as no pocket (the bottom edge
// stays flat there), so an open/close animation that drives depth 0 ->
// full grows the pocket smoothly out of a flat edge. Sockets are expected
// non-overlapping and within (cornerRadius, w - cornerRadius); the caller
// owns that invariant.
function buildBarPath(w, h, cornerRadius, connectorRadius, sockets) {
    var R = Math.max(0, Math.min(cornerRadius, w / 2, h / 2));

    // Depth below which a socket is treated as closed (flat edge). Just
    // under a pixel so the first animation frames don't pop a notch in.
    var MIN_DEPTH = 0.5;

    // Collect valid pockets and sort right-to-left: the bottom edge is
    // walked leftward (clockwise outline), so we weave sockets in
    // descending-x order.
    var pockets = [];
    if (sockets) {
        for (var i = 0; i < sockets.length; ++i) {
            var s = sockets[i];
            var depth = s.depth || 0;
            if (depth <= MIN_DEPTH)
                continue;
            var left = s.x;
            var right = s.x + s.width;
            // Clamp the fillet radius so the two vertical walls and the
            // two pocket-bottom corners always fit: r <= depth/2 keeps a
            // (possibly zero) wall between the concave top and convex
            // bottom corners; r <= width/2 keeps the bottom edge from
            // crossing itself. The outer Math.max(0, ...) guards a negative
            // connectorRadius (mirrors the R clamp above) so the arcs never
            // get a negative radius. As depth -> 0, r -> 0 and the pocket
            // collapses to a flat edge.
            var r = Math.max(0, Math.min(connectorRadius, depth / 2, s.width / 2));
            pockets.push({ left: left, right: right, depth: depth, r: r });
        }
        pockets.sort(function (a, b) { return b.left - a.left; });
    }

    var d = [];
    // Clockwise outline starting at the top-left, after the corner.
    d.push("M " + _n(R) + " 0");
    d.push("H " + _n(w - R));
    d.push("A " + _n(R) + " " + _n(R) + " 0 0 1 " + _n(w) + " " + _n(R));          // top-right (convex)
    d.push("V " + _n(h - R));
    d.push("A " + _n(R) + " " + _n(R) + " 0 0 1 " + _n(w - R) + " " + _n(h));      // bottom-right (convex)

    // Bottom edge, walking left and weaving each pocket.
    for (var p = 0; p < pockets.length; ++p) {
        var pk = pockets[p];
        var r = pk.r;
        var floorY = h + pk.depth;

        d.push("H " + _n(pk.right + r));                                            // edge up to the right inverted corner
        d.push("A " + _n(r) + " " + _n(r) + " 0 0 0 " + _n(pk.right) + " " + _n(h + r));       // right top: concave (inverted, sweep 0)
        d.push("V " + _n(floorY - r));                                              // down the right wall
        d.push("A " + _n(r) + " " + _n(r) + " 0 0 1 " + _n(pk.right - r) + " " + _n(floorY));  // bottom-right (convex)
        d.push("H " + _n(pk.left + r));                                             // across the pocket floor
        d.push("A " + _n(r) + " " + _n(r) + " 0 0 1 " + _n(pk.left) + " " + _n(floorY - r));   // bottom-left (convex)
        d.push("V " + _n(h + r));                                                   // up the left wall
        d.push("A " + _n(r) + " " + _n(r) + " 0 0 0 " + _n(pk.left - r) + " " + _n(h));        // left top: concave (inverted, sweep 0)
    }

    d.push("H " + _n(R));
    d.push("A " + _n(R) + " " + _n(R) + " 0 0 1 0 " + _n(h - R));                  // bottom-left (convex)
    d.push("V " + _n(R));
    d.push("A " + _n(R) + " " + _n(R) + " 0 0 1 " + _n(R) + " 0");                 // top-left (convex)
    d.push("Z");
    return d.join(" ");
}
