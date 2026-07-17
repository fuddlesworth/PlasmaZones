// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Sweep border surface shader — the Border pack's rounded-rect clip +
// border band, with the band colour replaced by a travelling two-colour
// sweep. An angular gradient (centred on the frame) circulates around the
// perimeter at p_sweepSpeed rotations per second, blending p_colorA and
// p_colorB in a seamless A -> B -> A loop; the sweep dims when the surface
// is unfocused so the motion reads as the active-window cue.
//
// The perimeter coordinate is the frame-normalised angle (divided by the
// half extents), so the sweep speed stays uniform per side on non-square
// windows — matching the marching / circuit / rgb packs. p_borderWidth /
// p_cornerRadius are logical px, scaled to device px by uSurfaceScale.
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

    // Travelling sweep: the fragment's aspect-normalised angle around the
    // frame centre, offset by time, picks a phase in a seamless two-colour
    // loop. cos makes the A -> B -> A blend continuous across the wrap, so
    // no seam ever shows.
    float angle = framePerimeter(p, bb.fs.center, bb.fs.halfSize); // -0.5 .. 0.5
    float phase = angle - iTime * p_sweepSpeed;
    vec4 sweep = mix(p_colorA, p_colorB, 0.5 - 0.5 * cos(phase * TAU));

    // Focus cue: full-strength sweep on the focused surface, dimmed otherwise.
    sweep.a *= focusDim(0.55);

    return borderComposite(tex, sweep, bb.edge, bb.insideMask);
}
