// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Glow surface shader — a soft edge bloom built from a separable Gaussian blur
// of the input surface (iChannel1, from buffer0.frag -> buffer1.frag). It is a
// pure compositing layer: it operates on whatever surface it receives (under a
// decoration chain that is the prior pack's output, e.g. the rounded bordered
// surface) and adds light. It does NOT know about borders or corner radius —
// it follows the input's own coverage (base.a), so it respects whatever shape
// earlier packs produced. Its strength and tint are this pack's parameters.
//
//   • Inner bloom — the blurred surface, tinted toward p_glowColor, screen-
//     blended into the content near the edge (exp falloff, masked by base.a so
//     it never lights the transparent outside and follows rounded corners).
//   • Outer halo — a soft bleed into any off-frame margin (drop-shadow region).
// Reach scales with surface size. Static (no iTime).

#version 450
#include <surface_uniforms.glsl>

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 base = surfaceTexel(vTexCoord);          // the input surface (prior pack's output)
    vec3 blur = texture(iChannel1, vTexCoord).rgb; // separable-Gaussian blurred surface
    vec3 glowTint = mix(blur, p_glowColor.rgb, 0.5);

    // Distance to the content rect (square — no radius; the input's own coverage
    // handles any rounding from earlier packs). <0 inside, >0 outside.
    vec2 p = surfacePixel(vTexCoord);
    vec2 halfSz = 0.5 * uSurfaceFrameSize;
    vec2 cen = uSurfaceFrameTopLeft + halfSz;
    vec2 q = abs(p - cen) - halfSz;
    float d = min(max(q.x, q.y), 0.0) + length(max(q, 0.0));

    float minDim = max(min(uSurfaceFrameSize.x, uSurfaceFrameSize.y), 1.0);
    float reach = clamp(0.10 * minDim, 16.0, 120.0);

    // Inner bloom, masked by the input coverage so it follows prior packs' shape.
    float inner = exp(-max(-d, 0.0) / (reach * 0.5)) * base.a;
    vec3 innerGlow = glowTint * (inner * p_glowStrength);
    vec3 lit = 1.0 - (1.0 - clamp(base.rgb, 0.0, 1.0)) * (1.0 - clamp(innerGlow, 0.0, 1.0));
    vec3 outRgb = mix(base.rgb, lit, clamp(inner, 0.0, 1.0));

    // Outer halo into any off-frame margin (where the texture has room).
    float outerA = exp(-max(d, 0.0) / (reach * 0.6)) * (1.0 - base.a) * (p_glowStrength * 0.7);
    outRgb += glowTint * outerA;
    float outA = clamp(base.a + outerA, 0.0, 1.0);

    fragColor = vec4(outRgb, outA);
}
