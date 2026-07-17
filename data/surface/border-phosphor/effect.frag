// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor border surface shader — the Border pack's rounded-rect clip +
// border band, with the band carrying the Phosphor brand accent gradient
// (cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E) flowing
// slowly around the perimeter. The gradient position ping-pongs so the
// cyan→rose sweep wraps seamlessly with no hue seam, and a soft white
// gleam orbits the band like light catching the frame. The window-side
// leg of the official Phosphor set (phosphor-flux / phosphor-bloom /
// desktop-phosphor). The perimeter coordinate is the frame-normalised
// angle (uniform speed per side on non-square windows, same approximation
// as the marching-ants pack). Dims when the surface is unfocused.
//
// ANIMATED (references iTime): metadata declares "animated": true so the
// daemon host ticks the item; the compositor detects the linked iTime
// uniform itself and repaints the window continuously while decorated.

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(p_colorCyan.rgb, p_colorBlue.rgb, clamp(t, 0.0, 1.0));
    c = mix(c, p_colorPurple.rgb, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, p_colorRose.rgb, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    // Band geometry: the family's OUTER-radius rounded-rect SDF, content clip
    // and band edge from this pack's logical-px width and corner radius.
    vec2 p = surfacePixel(uv);
    BorderBand bb = standardBorderBand(p, p_borderWidth, p_cornerRadius);

    // ── Flowing gradient: the perimeter coordinate drifts with time and is
    // folded into a ping-pong triangle, so the full cyan→rose gradient runs
    // out and back around the frame with no wrap seam.
    float u = framePerimeter(p, bb.fs.center, bb.fs.halfSize); // -0.5 .. 0.5
    float g = fract(u + iTime * p_flowSpeed);
    float tri = 1.0 - abs(2.0 * g - 1.0);
    vec3 col = fluxGradient(tri);

    // ── Orbiting gleam: one bright point travelling the band on its own
    // clock, a tight falloff along the perimeter coordinate (the same
    // wrap-around distance the circuit pack's pulses use).
    float gp = fract(u - iTime * p_gleamSpeed);
    float ring = min(gp, 1.0 - gp);
    float gleam = exp(-ring * ring * 900.0) * clamp(p_gleamStrength, 0.0, 1.0);
    col = mix(col, vec3(1.0), gleam * 0.6);

    vec4 band;
    band.rgb = col;
    band.a = clamp(clamp(p_opacity, 0.0, 1.0) * (1.0 + gleam * 0.3), 0.0, 1.0);

    // Focus cue: full-strength gradient on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
