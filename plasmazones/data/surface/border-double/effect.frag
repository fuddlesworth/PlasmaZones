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
    float aa = max(p_edgeSoftness, 0.001);

    // Bound the whole stack to half the frame, the way standardBorderBandSplit
    // does for the single-line packs and frameSdfSplit does for the radius. This pack
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
    // gap sit inside it and the content corner ends at p_cornerRadius. Each end
    // of the frame gets its own, so squaring the bottom corners squares them for
    // both lines and the content clip together rather than for none of them.
    //
    // A ZERO radius is left at zero rather than dilated to `total`, matching
    // standardBorderBandSplit. Dilating it would arc a corner the user asked to
    // be square by the whole stack width, which here is up to 52 logical px, and
    // the backdrop pack underneath draws its own square corner undilated. At zero
    // every level set is a sharp inset rect, so the stack comes out as concentric
    // mitred lines with a square gap and a square content clip.
    float radius = p_cornerRadius > 0.0 ? p_cornerRadius * uSurfaceScale + total : 0.0;
    float bottomRadius = surfaceBottomRadius(radius, p_roundBottomCorners);

    FrameSDF fs = frameSdfSplit(p, radius, bottomRadius);
    float d = fs.d;

    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    // Outer line: [-wOuter, 0]. Inner line: [-(total), -(wOuter + wGap)].
    //
    // A WIDTH OF ZERO MEANS NO LINE, and neither band gets that for free. Both
    // widths declare a minimum of 0, and this pack builds its bands from frameSdfSplit
    // rather than through standardBorderBandSplit, so it does not inherit that helper's
    // guard. Without these two tests: at wOuter 0 the outer term becomes
    // smoothstep(-aa, +aa, d) and insideMask its exact complement, so their product
    // peaks at 0.25 on the frame edge and paints a band about two feathers wide at a
    // quarter of the colour's alpha. At wInner 0 the two inner smoothsteps collapse
    // onto identical edges and (1 - S) * S peaks at the same 0.25, one gap in. The
    // user turns a line off and still sees it. Same defect, same remedy, as the six
    // controls standardBorderBand covers.
    float outerLine = wOuter > 0.0 ? smoothstep(-wOuter - aa, -wOuter + aa, d) : 0.0;
    float innerLine = wInner > 0.0
        ? (1.0 - smoothstep(-(wOuter + wGap) - aa, -(wOuter + wGap) + aa, d))
            * smoothstep(-total - aa, -total + aa, d)
        : 0.0;
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
