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
// Niri uniform shims (`niri_tex` → `uTexture0`; `niri_geo_to_tex` →
// identity mat3; `niri_random_seed` → `niri_random_seed_value()`) are
// provided by `<niri_compat.glsl>`. `texture2D` is rewritten to
// `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <niri_compat.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define smoothness    customParams[0].x
#define centerJitter  customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;
    float seed = niri_random_seed_value();

    float SQRT_2 = 1.414213562;

    vec2 center = vec2(0.5 + (seed - 0.5) * centerJitter, 0.5 + (seed * 0.7 - 0.35) * centerJitter);

    float dist = SQRT_2 * distance(center, uv);
    float m = smoothstep(-smoothness, 0.0, dist - p * (1.0 + smoothness));
    float reveal = 1.0 - m;

    vec3 tc = niri_geo_to_tex * vec3(uv, 1.0);
    vec4 color = texture(uTexture0, tc.st);

    fragColor = color * reveal;
}
