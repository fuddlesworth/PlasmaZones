// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Marching-ants border surface shader — the Border pack's rounded-rect
// clip + border band, with the band split into dashes that march around
// the perimeter (the classic selection marquee). The perimeter coordinate
// is the fragment's angle around the frame centre normalised by the
// frame's half extents, which keeps dash spacing roughly uniform per side
// on non-square windows (exact arc-length parameterisation of a rounded
// rect is not worth the shader cost for a decorative band). Dashes dim
// when the surface is unfocused, matching the sweep pack's focus cue.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    // Band geometry: the family's OUTER-radius rounded-rect SDF, content clip
    // and band edge from this pack's logical-px width and corner radius.
    vec2 p = surfacePixel(uv);
    BorderBand bb = standardBorderBand(p, p_borderWidth, p_cornerRadius);

    // Perimeter coordinate: angle normalised by the half extents so a wide
    // frame does not stretch its top and bottom dashes. Marching offset in
    // whole perimeter revolutions per second, like the sweep pack.
    float u = framePerimeter(p, bb.fs.center, bb.fs.halfSize); // -0.5 .. 0.5
    float cellPos = fract((u - iTime * p_marchSpeed) * max(p_dashCount, 1.0));
    // Antialias the dash edges over a fixed fraction of the cell so the
    // marquee reads crisp at any dash count without shimmering.
    float fill = clamp(p_dashFill, 0.0, 1.0);
    float dash = smoothstep(0.0, 0.06, cellPos) * (1.0 - smoothstep(fill - 0.06, fill, cellPos));

    vec4 band = mix(p_colorB, p_colorA, dash);

    // Focus cue: full-strength dashes on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
