// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixelfade Wave transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/pixelfade-wave).
// Wave-front pixelation — block size pulses along a diagonal wave
// that sweeps across the surface.
//
// Niri's pixelfade-wave ships symmetric close.glsl/open.glsl. PlasmaZones'
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
#define minBlocks  customParams[0].x
#define maxBlocks  customParams[0].y
#define waveSlope  customParams[0].z

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;

    float wave_x = (uv.x + uv.y) * 0.5;
    float wave_p = smoothstep(0.0, 1.0, p * 1.6 - wave_x * waveSlope);
    float bump = sin(wave_p * 3.14159);
    float blocks = mix(maxBlocks, minBlocks, bump);
    vec2 q = floor(uv * blocks) / blocks + 0.5 / blocks;

    vec3 tc = niri_geo_to_tex * vec3(q, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    float reveal = smoothstep(0.0, 1.0, wave_p);
    fragColor = win * reveal;
}
