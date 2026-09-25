// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Gradient border surface shader — the Border pack's rounded-rect clip +
// border band, with the band coloured by a STILL linear two-colour
// gradient across the frame at a configurable angle. The static sibling
// of the sweep pack: same band geometry, no time dependency, so the
// compositor never has to repaint the window for it. Dims when the
// surface is unfocused, matching the family's focus cue.

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    // Band geometry: the family's OUTER-radius rounded-rect SDF, content clip
    // and band edge from this pack's logical-px width and corner radius.
    vec2 p = surfacePixel(uv);
    float bottomRadius = p_roundBottomCorners >= 0.5 ? p_cornerRadius : 0.0;
    BorderBand bb = standardBorderBandSplit(p, p_borderWidth, p_cornerRadius, bottomRadius, p_edgeSoftness);

    // Frame-normalised position projected onto the gradient direction, so the
    // blend does not stretch with the window's aspect.
    //
    // It spans corner to corner at 0 and 90 degrees ONLY. On a diagonal the
    // frame-normalised corners project to +/-0.707, so t runs -0.207..1.207
    // and the smoothstep clips both ends: the ramp saturates about 71% of the
    // way to each corner and the outer stretch is flat colorA or colorB. That
    // is why it reads as working at the axis-aligned angles people try first.
    vec2 fuv = frameUv(p);
    float ang = radians(p_gradientAngle);
    vec2 dir = vec2(cos(ang), sin(ang));
    float t = smoothstep(0.0, 1.0, dot(fuv - 0.5, dir) + 0.5);
    vec4 band = mix(p_colorA, p_colorB, t);

    // Focus cue: full-strength band on the focused surface, dimmed otherwise.
    band.a *= focusDim(0.55);

    return borderComposite(tex, band, bb.edge, bb.insideMask);
}
