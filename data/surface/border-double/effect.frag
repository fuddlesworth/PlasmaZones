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

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 tex = surfaceTexel(vTexCoord);

    // Identity-decoration guard — mirrors border/effect.frag: a degenerate
    // frame rect would collapse the SDF to "edge everywhere".
    if (uSurfaceFrameSize.x < 1.0 || uSurfaceFrameSize.y < 1.0) {
        fragColor = tex;
        return;
    }

    vec2 p = surfacePixel(vTexCoord);
    const float aa = 0.7;

    float wOuter = p_outerWidth * uSurfaceScale;
    float wGap = p_gapWidth * uSurfaceScale;
    float wInner = p_innerWidth * uSurfaceScale;
    float total = wOuter + wGap + wInner;
    // OUTER radius = content radius + the full stack, so both lines and the
    // gap sit inside it and the content corner ends at p_cornerRadius.
    float radius = p_cornerRadius * uSurfaceScale + total;

    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    float r = clamp(radius, 0.0, min(halfSz.x, halfSz.y));

    vec2 q = abs(p - cen) - halfSz + r;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;

    float insideMask = 1.0 - smoothstep(-aa, aa, d);
    // Outer line: [-wOuter, 0]. Inner line: [-(total), -(wOuter + wGap)].
    float outerLine = smoothstep(-wOuter - aa, -wOuter + aa, d);
    float innerLine = (1.0 - smoothstep(-(wOuter + wGap) - aa, -(wOuter + wGap) + aa, d))
        * smoothstep(-total - aa, -total + aa, d);
    // Content is clipped inside the whole stack; the gap band between the
    // lines carries neither line nor content, showing what is behind.
    float stackEdge = smoothstep(-total - aa, -total + aa, d);

    // Focus cue: full-strength lines on the focused surface, dimmed otherwise.
    float focusDim = mix(0.55, 1.0, clamp(uSurfaceFocused, 0.0, 1.0));
    vec4 outerCol = p_colorA;
    vec4 innerCol = p_colorB;
    outerCol.a *= focusDim;
    innerCol.a *= focusDim;

    // Composite premultiplied: content inside the stack, then the two lines
    // over transparency — the gap band stays clear like the border pack's
    // translucent band, blending with whatever is behind the surface.
    float oa = outerLine * insideMask * outerCol.a;
    float ia = innerLine * innerCol.a;
    vec4 contentPx = tex * (1.0 - stackEdge);
    vec4 lines = vec4(outerCol.rgb * oa, oa) + vec4(innerCol.rgb * ia, ia) * (1.0 - oa);
    fragColor = lines + contentPx * (1.0 - lines.a);
}
