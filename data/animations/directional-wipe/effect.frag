// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Directional Wipe transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/directional-wipe).
// Diagonal soft-edge wipe with smoothstep falloff.
//
// Niri's directional-wipe ships symmetric close.glsl/open.glsl.
// PlasmaZones' runtime flips iTime on reverse legs (1→0 on close,
// 0→1 on open), so we use the niri OPEN body verbatim with
// `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)` and
// the runtime flip auto-mirrors the visual on close — no iIsReversed
// branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_dirX / p_dirY / p_wipeSmoothness (customParams[0].xyz) are generated
// from metadata.json — no hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the niri OPEN body auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
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
