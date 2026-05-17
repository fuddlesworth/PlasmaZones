// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Soft Warp Fade transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/soft-warp-fade). Subtle
// noise warp combined with eased fade — a gentle, organic transition.
//
// Niri's soft-warp-fade ships symmetric close.glsl/open.glsl — bodies
// are identical apart from `p = niri_clamped_progress` vs
// `p = 1.0 - niri_clamped_progress`, so the open leg is the close
// played in reverse. PlasmaZones already flips iTime on reverse legs
// (1→0 on close, 0→1 on open), so we use the niri OPEN body verbatim
// with `niri_clamped_progress` translated to `clamp(iTime, 0.0, 1.0)`
// and the runtime flip auto-mirrors the visual on close. No
// `iIsReversed` branch required.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. Niri's `swf_hash` /
// `swf_noise` helpers come from `<noise.glsl>` as `niriHash` / `niriNoise`.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define warpStrength customParams[0].x
#define noiseScale   customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    float strength = sin(p * 3.14159) * warpStrength;
    vec2 warp = vec2(
        niriNoise(uv * noiseScale + vec2(0.0, p * 0.5)),
        niriNoise(uv * noiseScale + vec2(p * 0.5, 0.0))
    ) - 0.5;
    vec2 warped = uv + warp * strength;

    // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
    vec4 win = surfaceColor(warped) * boundaryMask(warped);

    float t = smoothstep(0.05, 0.95, p);
    t = t * t * (3.0 - 2.0 * t);
    fragColor = win * t;
}
