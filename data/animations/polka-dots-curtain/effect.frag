// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Polka Dots Curtain transition — ported from liixini/shaders niri
// shader (https://github.com/liixini/shaders/tree/main/polka-dots-curtain).
// Polka-dots curtain — reveal threshold scales with distance from
// origin so dots open faster near a corner.
//
// Niri's polka-dots-curtain ships symmetric close.glsl/open.glsl —
// bodies are identical apart from `p = niri_clamped_progress` vs
// `p = 1.0 - niri_clamped_progress`, so the open leg is the close
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
#define dotCount customParams[0].x
#define centerX  customParams[0].y
#define centerY  customParams[0].z

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;
    vec3 tc = niri_geo_to_tex * vec3(uv, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    vec2 center = vec2(centerX, centerY);
    float reveal = step(distance(fract(uv * dotCount), vec2(0.5, 0.5)), p / max(distance(uv, center), 0.0001));

    fragColor = win * reveal;
}
