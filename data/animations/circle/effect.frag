// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Circle transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/circle). Circular
// reveal expanding from a slightly randomized center, with soft edge
// falloff.
//
// Niri's circle ships symmetric close.glsl/open.glsl — open uses
// `dist - p * (1 + smoothness)` and close uses
// `dist - (1 - p) * (1 + smoothness)`, which is the open formula
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we use the niri OPEN body verbatim
// with `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)`
// and the runtime flip auto-mirrors the visual on close. No
// `iIsReversed` branch required.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. niri's `niri_random_seed`
// is replaced by `surfaceSeed()` from `<noise.glsl>` (per-instance hash
// of `iSurfaceScreenPos.xy`). `texture2D` (GLSL ES) is rewritten to
// `texture` (GLSL 4.50 core) inline.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl (surfaceSeed) is pack-specific, so it stays here.
#include <noise.glsl>

// p_smoothness / p_centerJitter (customParams[0].xy) are generated from
// metadata.json — no hand-written slot #defines.

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the niri OPEN body auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
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
