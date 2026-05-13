// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Crosswarp transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/crosswarp). Diagonal
// warp wipe — the surface scales out from center while sweeping across.
//
// Niri's crosswarp ships symmetric close.glsl/open.glsl. PlasmaZones'
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
#define frontSpeed customParams[0].x

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    float x = smoothstep(0.0, 1.0, (p * frontSpeed + uv.x - 1.0));
    // niri's `clamp(warped, 0, 1)` was the bug: pinning off-window UVs
    // to the edge texel via clamp-to-edge produced a smeared border
    // around the warped silhouette. Drop the clamp and crop sampled
    // texels via boundaryMask (see noise.glsl) instead — same shape
    // as crosswarp wants (off-surface = transparent), but without the
    // edge-pixel bleed.
    vec2 warped = (uv - 0.5) * x + 0.5;

    vec4 win = texture(uTexture0, warped) * boundaryMask(warped);

    float in_bounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    fragColor = win * x * in_bounds;
}
