// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Plasma Flow transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/plasma-flow).
// Flowing plasma distortion — value-noise driven UV warp with a sin
// envelope on intensity.
//
// Niri's plasma-flow ships symmetric close.glsl/open.glsl. PlasmaZones'
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

// metadata.json declaration order → customParams[0] sub-slots
#define plasmaIntensity customParams[0].x
#define plasmaScale     customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float pf_hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float pf_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(pf_hash(i), pf_hash(i + vec2(1.0, 0.0)), f.x),
               mix(pf_hash(i + vec2(0.0, 1.0)), pf_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    vec2 flow = vec2(
        pf_noise(uv * plasmaScale + vec2(p * 2.0, 0.0)),
        pf_noise(uv * plasmaScale + vec2(0.0, p * 2.0))
    ) - 0.5;
    float intensity = sin(p * 3.14159) * plasmaIntensity;
    vec2 distorted = uv + flow * intensity;

    vec4 win = texture(uTexture0, distorted);

    float reveal = smoothstep(0.2, 0.8, p);
    fragColor = win * reveal;
}
