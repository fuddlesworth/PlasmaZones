// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Crosshatch transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/crosshatch). Crosshatch
// reveal — random diagonal threshold with radial falloff from center.
//
// Niri's crosshatch ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main().

// p_radialFalloff / p_edgeFade (customParams[0].xy) are generated from
// metadata.json — no hand-written slot #defines.

// classicHash (the (12.9898, 78.233) sin-dot value hash) hosted in
// shared/noise.glsl; the per-shader crosshatch_rand copy collapsed to it.
#include <noise.glsl>

// Symmetric: a single pTransition. `t` is the leg's iTime, which the runtime
// flips on the close leg (1→0), so the niri OPEN body auto-mirrors on close
// with no direction code.
vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(t, 0.0, 1.0);
    vec4 win = surfaceColor(uv);

    vec2 center = vec2(0.5);
    float dist = distance(center, uv) / max(p_radialFalloff, 1e-3);
    float r = p - min(classicHash(vec2(uv.y, 0.0)), classicHash(vec2(0.0, uv.x)));
    float reveal = mix(0.0, mix(step(dist, r), 1.0, smoothstep(1.0 - p_edgeFade, 1.0, p)), smoothstep(0.0, p_edgeFade, p));

    return win * reveal;
}
