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
// can't express both curves, so this is a pIn/pOut pair: the harness
// feeds forward 0→1 `t` to both and dispatches the matching niri body by
// leg direction (`windowFadingIn`).
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. niri's
// `niri_random_seed` is replaced by `surfaceSeed()` from `<noise.glsl>`.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl is pack-specific, so it stays here.
#include <noise.glsl>

// p_rippleAmplitude / p_rippleSpeed (customParams[0].xy) are generated from
// metadata.json. Same params drive BOTH legs — the asymmetry lives in the
// intensity / alpha curves, not in amplitude or speed.

// `uv` is vTexCoord; `t` is the forward 0→1 leg progress (the harness applies
// legProgress()); `windowFadingIn` selects the niri open vs close body.
vec4 rippleBody(vec2 uv, float t, bool windowFadingIn) {
    vec4 result;
    if (!windowFadingIn) {
        // ── niri close.glsl body (forward progress p = t) ──
        float p = t;
        float seed = surfaceSeed() * 6.28318;

        vec2 dir = uv - vec2(0.5);
        float dist = length(dir);

        float intensity = p * p;
        vec2 offset = dir * (sin(p * dist * p_rippleAmplitude - p * p_rippleSpeed + seed) + 0.5) / 30.0;

        vec2 wuv = uv + offset * intensity;
        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(wuv) * boundaryMask(wuv);

        float alpha = smoothstep(1.0, 0.5, p);
        result = color * alpha;
    } else {
        // ── niri open.glsl body (forward progress p = t) ──
        float p = t;
        float seed = surfaceSeed() * 6.28318;

        vec2 dir = uv - vec2(0.5);
        float dist = length(dir);

        float intensity = (1.0 - p) * (1.0 - p);
        vec2 offset = dir * (sin(p * dist * p_rippleAmplitude - p * p_rippleSpeed + seed) + 0.5) / 30.0;

        vec2 wuv = uv + offset * intensity;
        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(wuv) * boundaryMask(wuv);

        float alpha = smoothstep(0.0, 0.3, p);
        result = color * alpha;
    }
    return result;
}

vec4 pIn(vec2 uv, float t)  { return rippleBody(uv, t, true);  }
vec4 pOut(vec2 uv, float t) { return rippleBody(uv, t, false); }
