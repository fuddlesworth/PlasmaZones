// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ripple transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/ripple). Outward radial
// ripple — sin-modulated UV displacement with seed-stable per-instance
// phase. Asymmetric envelope and alpha curves between open and close.
//
// Niri's ripple ships genuinely different close.glsl and open.glsl
// bodies — close uses `intensity = p*p` with `alpha = smoothstep(1.0,
// 0.5, p)`; open uses `intensity = (1-p)*(1-p)` with
// `alpha = smoothstep(0.0, 0.3, p)`. The runtime's iTime flip alone
// can't express both curves, so we branch on iIsReversed to dispatch
// the matching niri body for each leg.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. niri's
// `niri_random_seed` is replaced by `surfaceSeed()` from `<noise.glsl>`.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// Same params drive BOTH open/close legs — the asymmetry lives in
// intensity / alpha curves, not in amplitude or speed.
#define rippleAmplitude customParams[0].x
#define rippleSpeed     customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main() {
    vec4 result;
    if (iIsReversed != 0) {
        // ── niri close.glsl body ──
        // close.glsl declares `float p = niri_clamped_progress;`. On the
        // PlasmaZones close leg, niri_clamped_progress = 1 - iTime, so:
        //   `float p = 1.0 - clamp(iTime, 0.0, 1.0);`
        float p = 1.0 - clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 6.28318;

        vec2 dir = uv - vec2(0.5);
        float dist = length(dir);

        float intensity = p * p;
        vec2 offset = dir * (sin(p * dist * rippleAmplitude - p * rippleSpeed + seed) + 0.5) / 30.0;

        vec2 wuv = uv + offset * intensity;
        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = texture(uTexture0, wuv) * boundaryMask(wuv);

        float alpha = smoothstep(1.0, 0.5, p);
        result = color * alpha;
    } else {
        // ── niri open.glsl body ──
        // open.glsl declares `float p = niri_clamped_progress;`. On the
        // open leg, niri_clamped_progress = iTime, so:
        //   `float p = clamp(iTime, 0.0, 1.0);`
        float p = clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 6.28318;

        vec2 dir = uv - vec2(0.5);
        float dist = length(dir);

        float intensity = (1.0 - p) * (1.0 - p);
        vec2 offset = dir * (sin(p * dist * rippleAmplitude - p * rippleSpeed + seed) + 0.5) / 30.0;

        vec2 wuv = uv + offset * intensity;
        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = texture(uTexture0, wuv) * boundaryMask(wuv);

        float alpha = smoothstep(0.0, 0.3, p);
        result = color * alpha;
    }
    fragColor = result;
}
