// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Circle transition — a circular reveal expanding from a slightly
// randomized center, with soft edge falloff. Inspired by liixini/shaders'
// niri circle shader.
//
// Symmetric transition, written as a single `pTransition`. `t` is the
// leg's iTime, which the runtime flips on reverse legs (0→1 on open,
// 1→0 on close), so the reveal reads `clamp(t, 0.0, 1.0)` directly and
// the close leg plays in reverse automatically, with no `iIsReversed`
// branch. Geometry and texture coordinates coincide here, so
// `texture(uTexture0, uv)` samples directly, and per-instance variation
// comes from `surfaceSeed()` in `<noise.glsl>` (a hash of
// `iSurfaceScreenPos.xy`).

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl (surfaceSeed) is pack-specific, so it stays here.
#include <noise.glsl>

// p_smoothness / p_centerJitter (customParams[0].xy) are generated from
// metadata.json — no hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the reveal auto-mirrors on close with no
// direction code.
vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    float seed = surfaceSeed();

    float SQRT_2 = 1.414213562;

    vec2 center = vec2(0.5 + (seed - 0.5) * p_centerJitter, 0.5 + (seed * 0.7 - 0.35) * p_centerJitter);

    float dist = SQRT_2 * distance(center, uv);
    float m = smoothstep(-p_smoothness, 0.0, dist - p * (1.0 + p_smoothness));
    float reveal = 1.0 - m;

    vec4 color = surfaceColor(uv);

    return color * reveal;
}
