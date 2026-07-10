// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Drop-shadow surface shader — the glow pack's analytic rounded-rect SDF
// falloff, recast as a proper window shadow: dark instead of coloured,
// displaced by a configurable offset so it reads as light coming from
// above, and only mildly focus-dimmed (a real shadow does not vanish when
// the window loses focus, it just softens). The content passes through
// byte-for-byte; the shadow lives purely in the transparent margin.
//
// CAPTURE MARGIN: metadata declares `"paddingParam": "shadowSize"`, so the
// compositor host inflates the capture canvas by the resolved size. The
// offset displaces the shadow within that margin; the texture-edge feather
// below keeps a large offset from cutting off in a hard rectangle at the
// canvas boundary. Static (no iTime).

vec4 pSurface(vec2 uv) {
    vec4 base = surfaceTexel(uv);

    // Degenerate frame guard — mirrors glow/effect.frag: an unwired frame
    // rect would bleed shadow over the whole surface for a transient frame.
    if (surfaceFrameDegenerate()) {
        return base;
    }

    // Rounded-rect SDF over the frame rect DISPLACED by the cast offset:
    // evaluating the fragment against the shifted frame moves the whole
    // shadow body down/right, the classic dropped look.
    vec2 offset = vec2(p_offsetX, p_offsetY) * uSurfaceScale;
    vec2 p = surfacePixel(uv) - offset;
    FrameSDF fs = frameSdf(p, p_cornerRadius * uSurfaceScale);

    // Same exp(-4t²) reach falloff as the glow pack, but the edge feather is
    // evaluated at the REAL (undisplaced) fragment position so a large offset
    // pushing the shadow toward the canvas edge fades out instead of ending in
    // a hard rectangle. Confined to the transparent margin and only mildly
    // focus-softened (a real shadow persists unfocused) — the shared halo.
    float reach = max(p_shadowSize * uSurfaceScale, 1.0);
    float body = haloFalloff(fs.d, reach, surfacePixel(uv), base.a, p_shadowStrength, 0.65);

    // Premultiplied over: the dark veil fills the margin under its own
    // alpha; with the default black colour the rgb term contributes nothing
    // and the shadow reads as pure darkening of whatever is behind.
    float sa = clamp(body * p_shadowColor.a, 0.0, 1.0);
    return marginComposite(base, p_shadowColor.rgb, sa);
}
