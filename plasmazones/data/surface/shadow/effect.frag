// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Drop-shadow surface shader — the glow pack's analytic rounded-rect SDF
// falloff, recast as a proper window shadow: dark instead of coloured,
// displaced by a configurable offset so it reads as light coming from
// above, and only mildly focus-dimmed (a real shadow does not vanish when
// the window loses focus, it just softens). The content passes through
// byte-for-byte; the shadow lives in the transparent margin and, on a window
// whose own body is translucent, in the band within two reaches inside the frame.
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
    //
    // Top and bottom radii are separate so the shadow traces the SAME outline as
    // the backdrop pack under it. A pane squared off along its bottom edge used
    // to cast a shadow still rounded at the corners the pane had given up.
    vec2 offset = vec2(p_offsetX, p_offsetY) * uSurfaceScale;
    vec2 realPx = surfacePixel(uv);
    vec2 p = realPx - offset;
    float cornerPx = p_cornerRadius * uSurfaceScale;
    float bottomPx = surfaceBottomRadius(cornerPx, p_roundBottomCorners);
    FrameSDF fs = frameSdfSplit(p, cornerPx, bottomPx);

    // Same exp(-4t²) reach falloff as the glow pack, but the edge feather is
    // evaluated at the REAL (undisplaced) fragment position so a large offset
    // pushing the shadow toward the canvas edge fades out instead of ending in
    // a hard rectangle. Held to the margin and the band within two reaches inside
    // the frame, and only mildly
    // focus-softened (a real shadow persists unfocused) — the shared halo.
    // Top radius for the depth gate at both ends, as in the glow pack, and the same
    // known limitation: fs carries the split outline, the gate does not, so at a
    // squared bottom corner the gate over-KEEPS instead of holding the veil to two
    // reaches inside the frame. Only reachable when cornerRadius > 3.41 * shadowSize
    // on a translucent body, so never at the bundled defaults. See glow/effect.frag
    // for why splitting it is not a drive-by.
    float reach = max(p_shadowSize * uSurfaceScale, 1.0);
    float body = haloFalloff(fs.d, reach, realPx, base.a, p_shadowStrength, 0.65, cornerPx);

    // Premultiplied over: the dark veil fills the margin under its own
    // alpha; with the default black colour the rgb term contributes nothing
    // and the shadow reads as pure darkening of whatever is behind.
    float sa = clamp(body * p_shadowColor.a, 0.0, 1.0);
    return marginComposite(base, p_shadowColor.rgb, sa);
}
