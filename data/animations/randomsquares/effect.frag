// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Random Squares transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/randomsquares).
// Random-grid block reveal — each cell flips on at its own threshold
// with a soft window.
//
// Niri's randomsquares ships symmetric close.glsl/open.glsl.
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

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define gridDensity     customParams[0].x
#define cellSmoothness  customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float rs_rand(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;
    vec4 win = texture(uTexture0, uv);

    vec2 sz = vec2(gridDensity);
    float r = rs_rand(floor(sz * uv));
    float reveal = smoothstep(0.0, -cellSmoothness, r - (p * (1.0 + cellSmoothness)));

    fragColor = win * reveal;
}
