// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Double border surface shader — the Border pack's rounded-rect clip with
// TWO concentric border lines separated by a clear gap, each line with
// its own width and colour. One SDF over the outer rounded rect drives
// both bands and the content clip: the outer line occupies the first
// band inward, the gap shows whatever is behind the surface, and the
// inner line frames the content. Static, so the compositor never has to
// repaint the window for it. Dims when the surface is unfocused,
// matching the family's focus cue.

vec4 pSurface(vec2 uv) {
    vec4 tex = surfaceTexel(uv);

    if (surfaceFrameDegenerate()) {
        return tex;
    }

    vec2 p = surfacePixel(uv);
    const float aa = 0.7;

    // Bound the whole stack to half the frame, the way standardBorderBand does
    // for the single-line packs and frameSdf does for the radius. This pack
    // builds its own bands rather than calling that helper, so without this a
    // stack wider than the frame's half extent leaves every interior fragment
    // past the smoothstep and paints the surface as solid border with the
    // content multiplied away. Scaled first, then scaled back proportionally,
    // so the outer/gap/inner ratio the user chose is preserved.
    // 0.9 of the half extent, not the whole of it: clamping flush would leave
    // the entire interior on the band side of the smoothstep, so the content
    // would still be wiped. Same fraction the glass pack uses for its edge.
    vec2 halfSize = 0.5 * uSurfaceFrameSize;
    float limit = max(0.9 * min(halfSize.x, halfSize.y), 0.1);
    float wOuter = p_outerWidth * uSurfaceScale;
    float wGap = p_gapWidth * uSurfaceScale;
    float wInner = p_innerWidth * uSurfaceScale;
    float total = wOuter + wGap + wInner;
    if (total > limit && total > 0.0) {
        float k = limit / total;
        wOuter *= k;
        wGap *= k;
        wInner *= k;
        total = limit;
    }
    // OUTER radius = content radius + the full stack, so both lines and the
    // gap sit inside it and the content corner ends at p_cornerRadius.
    float radius = p_cornerRadius * uSurfaceScale + total;

    FrameSDF fs = frameSdf(p, radius);
    float d = fs.d;

    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    // Outer line: [-wOuter, 0]. Inner line: [-(total), -(wOuter + wGap)].
    float outerLine = smoothstep(-wOuter - aa, -wOuter + aa, d);
    float innerLine = (1.0 - smoothstep(-(wOuter + wGap) - aa, -(wOuter + wGap) + aa, d))
        * smoothstep(-total - aa, -total + aa, d);
    // Content is clipped inside the whole stack; the gap band between the
    // lines carries neither line nor content, showing what is behind.
    float stackEdge = smoothstep(-total - aa, -total + aa, d);

    // Focus cue: full-strength lines on the focused surface, dimmed otherwise.
    float dim = focusDim(0.55);
    vec4 outerCol = p_colorA;
    vec4 innerCol = p_colorB;
    outerCol.a *= dim;
    innerCol.a *= dim;

    // Composite premultiplied: content inside the stack, then the two lines
    // over transparency — the gap band stays clear like the border pack's
    // translucent band, blending with whatever is behind the surface.
    float oa = outerLine * insideMask * outerCol.a;
    float ia = innerLine * innerCol.a;
    vec4 contentPx = tex * (1.0 - stackEdge);
    vec4 lines = vec4(outerCol.rgb * oa, oa) + vec4(innerCol.rgb * ia, ia) * (1.0 - oa);
    return lines + contentPx * (1.0 - lines.a);
}
