// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Voronoi Shatter transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/voronoi-shatter). Voronoi-
// cell shatter — each shard reveals at its own threshold via per-cell
// hash.
//
// Niri's voronoi-shatter ships symmetric close.glsl/open.glsl.
// PlasmaZones' runtime flips iTime on reverse legs (1→0 on close, 0→1
// on open), so we use the niri OPEN body verbatim with
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

// metadata.json declaration order → customParams[0] sub-slots.
#define cellDensity   customParams[0].x
#define revealSpread  customParams[0].y
#define shardSoftness customParams[0].z

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

vec2 vs_hash2(vec2 p) {
    return fract(sin(vec2(dot(p, vec2(127.1, 311.7)),
                           dot(p, vec2(269.5, 183.3)))) * 43758.5453);
}

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;
    vec4 win = texture(uTexture0, uv);

    float scale = cellDensity;
    vec2 q = uv * scale;
    vec2 g = floor(q);
    vec2 f = fract(q);
    float min_d = 100.0;
    vec2 cell = g;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 nb = vec2(float(x), float(y));
            vec2 r = nb + vs_hash2(g + nb) - f;
            float d = dot(r, r);
            if (d < min_d) { min_d = d; cell = g + nb; }
        }
    }
    float seed = vs_hash2(cell).x;
    float shard_p = smoothstep(seed * 0.5, seed * 0.5 + revealSpread, p);
    float reveal = smoothstep(0.0, shardSoftness, shard_p);

    fragColor = win * reveal;
}
