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
// rewritten to `texture` (GLSL 4.50 core) inline. Niri's `swf_hash` and
// `swf_noise` helpers lift to file scope unchanged.

#version 450

#include <animation_uniforms.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define warpStrength customParams[0].x
#define noiseScale   customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float swf_hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float swf_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(swf_hash(i), swf_hash(i + vec2(1.0, 0.0)), f.x),
               mix(swf_hash(i + vec2(0.0, 1.0)), swf_hash(i + vec2(1.0, 1.0)), f.x), f.y);
}

void main() {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(iTime, 0.0, 1.0);
    vec2 uv = vTexCoord;

    float strength = sin(p * 3.14159) * warpStrength;
    vec2 warp = vec2(
        swf_noise(uv * noiseScale + vec2(0.0, p * 0.5)),
        swf_noise(uv * noiseScale + vec2(p * 0.5, 0.0))
    ) - 0.5;
    vec2 warped = uv + warp * strength;

    // Soft inside-mask. The warp above pushes UVs slightly past [0, 1] at
    // boundary fragments, and `uTexture0` is clamp-to-edge — the typical
    // edge alpha is 0 (window shadow / rounded corners) so samples beyond
    // the surface produce a grey-transparent border. Fade to zero across
    // a tight 0.005-wide band at each edge so the warped silhouette crops
    // cleanly. Same pattern as morph/plasma-flow.
    vec2 insideLo = smoothstep(vec2(-0.005), vec2(0.0), warped);
    vec2 insideHi = vec2(1.0) - smoothstep(vec2(1.0), vec2(1.005), warped);
    float mask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;
    vec4 win = texture(uTexture0, warped) * mask;

    float t = smoothstep(0.05, 0.95, p);
    t = t * t * (3.0 - 2.0 * t);
    fragColor = win * t;
}
