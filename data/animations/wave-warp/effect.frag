// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wave Warp transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/wave-warp).
// Soft directional wave wipe with scale-warp — the surface contracts
// inward as it sweeps across.
//
// Niri's wave-warp ships symmetric close.glsl/open.glsl. PlasmaZones'
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

// metadata.json declaration order → customParams[0] sub-slots.
// `waveAngle` (radians) replaces niri's hardcoded `vec2 dir = (1, 0)`
// — single dial avoids the NaN risk of normalising a user-supplied
// vec2 that could come in as (0, 0).
#define waveSmoothness customParams[0].x
#define waveAngle      customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;

    vec2 dir = vec2(cos(waveAngle), sin(waveAngle));
    vec2 v = normalize(dir);
    v /= abs(v.x) + abs(v.y);
    float d = v.x * 0.5 + v.y * 0.5;
    float m = 1.0 - smoothstep(-waveSmoothness, 0.0, v.x * uv.x + v.y * uv.y - (d - 0.5 + p * (1.0 + waveSmoothness)));

    vec2 warped = clamp((uv - 0.5) * m + 0.5, vec2(0.0), vec2(1.0));
    vec3 tc = niri_geo_to_tex * vec3(warped, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    float in_bounds = step(0.0, uv.x) * step(uv.x, 1.0) * step(0.0, uv.y) * step(uv.y, 1.0);
    fragColor = win * m * in_bounds;
}
