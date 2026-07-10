// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow surface shader — a soft COLOURED SHADOW hugging the surface edge, in
// the spirit of the Oxygen decoration's active-window glow. The content
// passes through untouched; a Gaussian-profile halo starts at the frame edge
// and fades out over p_glowSize logical px.
//
// Analytic, single-pass: the halo is a rounded-rect SDF falloff over the
// frame rect (the same SDF the border pack uses, with this pack's own
// p_cornerRadius so it hugs the rounded corners when stacked after Border),
// NOT a blur of the capture. The earlier blur-based version lit the whole
// shadow margin at near-constant strength and cut off in a hard rectangle at
// the capture boundary; the SDF gives an exact, tunable reach and a shadow-
// like exp(-x²) profile that is brightest against the edge and gone well
// before the margin ends.
//
// CAPTURE MARGIN: metadata declares `"paddingParam": "glowSize"`, so the
// compositor host inflates the window's capture canvas by the resolved glow
// size — the halo has real transparent margin to draw into even when the
// window has no decoration shadow (e.g. a hidden title bar making it
// borderless). The texture-edge feather below still guarantees a clean
// fade-out on hosts that provide less margin than the reach (the daemon's
// host-defined surfaces).
//
// Focus-tracking like Oxygen: full strength on the focused surface, dimmed
// otherwise. Static (no iTime).

vec4 pSurface(vec2 uv) {
    vec4 base = surfaceTexel(uv);          // the input surface (prior pack's output)

    // Degenerate frame (a host that has not wired geometry yet): the SDF
    // below would collapse to the top-left point and bleed halo over the
    // whole surface for that transient, so pass the content through untouched
    // until a real frame arrives. Mirrors border/effect.frag's guard.
    if (surfaceFrameDegenerate()) {
        return base;
    }

    // Rounded-rect SDF over the frame rect (same construction as the border
    // pack). d > 0 outside the frame — the region the halo lives in.
    vec2 p = surfacePixel(uv);
    FrameSDF fs = frameSdf(p, p_cornerRadius * uSurfaceScale);

    // Gaussian-profile reach falloff (exp(-4t²), a soft shadow not a flood),
    // feathered to zero just inside the texture edge so a slim capture margin
    // yields a smaller glow instead of a hard-cut rectangle, confined to the
    // transparent margin, and focus-dimmed like Oxygen's active-window cue —
    // the shared glow/shadow halo.
    float reach = max(p_glowSize * uSurfaceScale, 1.0);
    float halo = haloFalloff(fs.d, reach, p, base.a, p_glowStrength, 0.30);

    // Premultiplied additive-over: the halo lights the margin under its own
    // alpha; the content term is untouched.
    float haloA = clamp(halo * p_glowColor.a, 0.0, 1.0);
    return marginComposite(base, p_glowColor.rgb, haloA);
}
