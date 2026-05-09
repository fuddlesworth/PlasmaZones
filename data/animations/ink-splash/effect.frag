// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ink Splash transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/ink-splash). Ink-blot
// reveal — fbm-distorted radial threshold blooms outward like spilled
// ink.
//
// Niri's ink-splash ships symmetric close.glsl/open.glsl. PlasmaZones'
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
#define blobScale    customParams[0].x
#define fingerScale  customParams[0].y
#define splashSpeed  customParams[0].z
#define edgeSoftness customParams[0].w

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float is_hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float is_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(is_hash(i), is_hash(i + vec2(1.0, 0.0)), f.x),
               mix(is_hash(i + vec2(0.0, 1.0)), is_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

float is_fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; i++) {
        v += amp * is_noise(p);
        p *= 2.1;
        amp *= 0.5;
    }
    return v;
}

void main() {
    vec3 coords_geo = vec3(vTexCoord, 1.0);
    vec3 size_geo = vec3(max(iAnchorSize, vec2(1.0)), 1.0);

    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = coords_geo.xy;
    vec3 tc = niri_geo_to_tex * vec3(uv, 1.0);
    vec4 win = texture(uTexture0, tc.st);

    float blob = is_fbm(uv * blobScale);
    float fingers = is_fbm(uv * fingerScale);
    float distortion = (blob - 0.5) * 0.5 + (fingers - 0.5) * 0.18;
    vec2 c = uv - vec2(0.5);
    c.x *= size_geo.x / max(size_geo.y, 0.0001);
    float d = length(c);
    float splash_d = d + distortion;
    float boundary = p * splashSpeed - 0.15;
    float diff = splash_d - boundary;
    float reveal = smoothstep(edgeSoftness, -edgeSoftness, diff);

    fragColor = win * reveal;
}
