// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Flyeye transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/flyeye).
// Faceted-lens distortion — UV oscillates with cosine/sine grid,
// settling as the surface fades.
//
// Niri's flyeye ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body verbatim with `niri_clamped_progress`
// translated to `clamp(iTime, 0.0, 1.0)` and the runtime flip
// auto-mirrors the visual on close — no iIsReversed branch needed.
//
// Niri uniform shims (`niri_tex` → `uTexture0`; `niri_geo_to_tex` →
// identity mat3; `niri_random_seed` → `niri_random_seed_value()`) are
// provided by `<niri_compat.glsl>`. `texture2D` is rewritten to
// `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <niri_compat.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define displacement    customParams[0].x
#define facetFrequency  customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;

    float inv = 1.0 - p;
    vec2 disp = displacement * vec2(cos(facetFrequency * uv.x), sin(facetFrequency * uv.y));
    vec2 sample_uv = uv + inv * disp;

    vec3 tc = niri_geo_to_tex * vec3(sample_uv, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    fragColor = win * p;
}
