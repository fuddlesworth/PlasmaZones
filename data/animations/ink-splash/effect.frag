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
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define blobScale    customParams[0].x
#define fingerScale  customParams[0].y
#define splashSpeed  customParams[0].z
#define edgeSoftness customParams[0].w

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float is_fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; i++) {
        v += amp * niriNoise(p);
        p *= 2.1;
        amp *= 0.5;
    }
    return v;
}

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;
    vec4 win = surfaceColor(uv);

    float blob = is_fbm(uv * blobScale);
    float fingers = is_fbm(uv * fingerScale);
    float distortion = (blob - 0.5) * 0.5 + (fingers - 0.5) * 0.18;
    vec2 c = uv - vec2(0.5);
    c.x *= iAnchorSize.x / max(iAnchorSize.y, 0.0001);
    float d = length(c);
    float splash_d = d + distortion;
    float boundary = p * splashSpeed - 0.15;
    float diff = splash_d - boundary;
    float reveal = smoothstep(edgeSoftness, -edgeSoftness, diff);

    fragColor = win * reveal;
}
