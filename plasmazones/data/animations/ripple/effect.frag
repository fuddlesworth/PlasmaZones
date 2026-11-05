// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Ripple transition — an outward radial ripple with sin-modulated UV
// displacement and a seed-stable per-instance phase. Inspired by
// liixini/shaders' niri ripple shader.
//
// The open and close legs use genuinely different curves — close uses
// `intensity = p*p` with `alpha = smoothstep(1.0, 0.5, p)`; open uses
// `intensity = (1-p)*(1-p)` with `alpha = smoothstep(0.0, 0.3, p)`. The
// runtime's iTime flip alone can't express both, so this is a pIn/pOut
// pair: the harness feeds forward 0→1 `t` to both and dispatches the
// matching body by leg direction (`windowFadingIn`).
//
// Geometry and texture coordinates coincide here, so
// `texture(uTexture0, uv)` samples directly, and per-instance variation
// comes from `surfaceSeed()` in `<noise.glsl>`.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl is pack-specific, so it stays here.
#include <noise.glsl>

// p_rippleAmplitude / p_rippleSpeed (customParams[0].xy) are generated from
// metadata.json. Same params drive BOTH legs — the asymmetry lives in the
// intensity / alpha curves, not in amplitude or speed.

// `uv` is vTexCoord; `t` is the forward 0→1 leg progress (the harness applies
// legProgress()); `windowFadingIn` selects the open vs close body.
vec4 rippleBody(vec2 uv, float t, bool windowFadingIn) {
    vec4 result;
    if (!windowFadingIn) {
        // ── close-leg body (forward progress p = t) ──
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
        // ── open-leg body (forward progress p = t) ──
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
