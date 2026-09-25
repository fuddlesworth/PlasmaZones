// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Crosshatch transition — a crosshatch reveal driven by a random
// diagonal threshold with radial falloff from the center. Inspired by
// liixini/shaders' niri crosshatch shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_radialFalloff / p_edgeFade (customParams[0].xy) are generated from
// metadata.json — no hand-written slot #defines.

// classicHash (the (12.9898, 78.233) sin-dot value hash) hosted in
// shared/noise.glsl; the per-shader crosshatch_rand copy collapsed to it.
#include <noise.glsl>

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the reveal auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    vec2 center = vec2(0.5);
    float dist = distance(center, uv) / max(p_radialFalloff, 1e-3);
    float r = p - min(classicHash(vec2(uv.y, 0.0)), classicHash(vec2(0.0, uv.x)));
    float reveal = mix(0.0, mix(step(dist, r), 1.0, smoothstep(1.0 - p_edgeFade, 1.0, p)), smoothstep(0.0, p_edgeFade, p));

    return win * reveal;
}
