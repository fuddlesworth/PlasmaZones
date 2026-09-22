// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Directional Wipe transition — a diagonal soft-edge wipe with a
// smoothstep falloff. Inspired by liixini/shaders' niri directional-wipe
// shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime, 0.0, 1.0)` directly and the close leg
// plays in reverse automatically, with no `iIsReversed` branch. Geometry
// and texture coordinates coincide here, so `texture(uTexture0, uv)`
// samples directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_dirX / p_dirY / p_wipeSmoothness (customParams[0].xyz) are generated
// from metadata.json — no hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the reveal auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    vec2 dir = vec2(p_dirX, p_dirY);
    // Defend against (0,0) which would NaN through normalize().
    // magnitude < 1e-6 falls back to vertical default.
    if (length(dir) < 1e-6) {
        dir = vec2(1.0, -1.0);
    }
    vec2 center = vec2(0.5, 0.5);
    vec2 v = normalize(dir);
    v /= abs(v.x) + abs(v.y);
    float d = v.x * center.x + v.y * center.y;
    float reveal = (1.0 - step(p, 0.0)) *
        (1.0 - smoothstep(-p_wipeSmoothness, 0.0, v.x * uv.x + v.y * uv.y - (d - 0.5 + p * (1.0 + p_wipeSmoothness))));

    return win * reveal;
}
