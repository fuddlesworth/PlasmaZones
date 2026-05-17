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
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define displacement    customParams[0].x
#define facetFrequency  customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    float inv = 1.0 - p;
    vec2 disp = displacement * vec2(cos(facetFrequency * uv.x), sin(facetFrequency * uv.y));
    vec2 sample_uv = uv + inv * disp;

    // boundaryMask (see noise.glsl) crops samples outside [0, 1] —
    // the cos/sin facet displacement carries `sample_uv` past the
    // anchor edge along the lens facets, and uTexture0's clamp-to-
    // edge sampler would otherwise smear the rim texels into the
    // facet ripple.
    vec4 win = surfaceColor(sample_uv) * boundaryMask(sample_uv);

    fragColor = win * p;
}
