// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Smoke transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/smoke). Domain-warped
// fbm smoke — the surface dissipates into curling smoke. Asymmetric
// reveal vs. dissolve formula.
//
// Niri's smoke ships asymmetric close.glsl/open.glsl — the formulas
// genuinely differ (close uses `dissolve = (1-dist)*1.2 + fluid*0.7;
// remain = smoothstep(...,p*1.8); tail = smoothstep(1.0,0.8,p)` while
// open uses `appear = (1-dist*1.2) + (1-fluid)*0.7; reveal =
// smoothstep(...,(1-p)*1.8)`). Branch on iIsReversed; PlasmaZones
// flips iTime on the close leg, so the niri close branch's
// `p = niri_clamped_progress` becomes `p = 1.0 - clamp(iTime, 0.0, 1.0)`
// (per translation rules) and open's `p = niri_clamped_progress` becomes
// `p = clamp(iTime, 0.0, 1.0)`.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. niri's
// `niri_random_seed` is replaced by `surfaceSeed()` from `<noise.glsl>`.

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots. Both
// iIsReversed branches share the same params: `smokeDistortion` is the
// close-leg coefficient (default 0.4) and the open leg uses
// `smokeDistortion * 0.875` to preserve the niri 0.4-vs-0.35 ratio so
// defaults reproduce the original visual exactly.
#define smokeNoiseScale      customParams[0].x
#define smokeSwirlSpeed      customParams[0].y
#define smokeVerticalSquish  customParams[0].z
#define smokeDistortion      customParams[0].w

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float sm_hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float sm_noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = sm_hash(i);
    float b = sm_hash(i + vec2(1.0, 0.0));
    float c = sm_hash(i + vec2(0.0, 1.0));
    float d = sm_hash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float sm_fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 6; i++) {
        v += amp * sm_noise(p);
        p *= 2.0;
        amp *= 0.5;
    }
    return v;
}

float sm_warpedFbm(vec2 p, float t) {
    vec2 q = vec2(sm_fbm(p + vec2(0.0, 0.0)),
                  sm_fbm(p + vec2(5.2, 1.3)));

    vec2 r = vec2(sm_fbm(p + 6.0 * q + vec2(1.7, 9.2) + 0.25 * t),
                  sm_fbm(p + 6.0 * q + vec2(8.3, 2.8) + 0.22 * t));

    vec2 s = vec2(sm_fbm(p + 5.0 * r + vec2(3.1, 7.4) + 0.18 * t),
                  sm_fbm(p + 5.0 * r + vec2(6.7, 0.9) + 0.2 * t));

    return sm_fbm(p + 6.0 * s);
}

void main() {
    vec4 result;
    if (iIsReversed != 0) {
        // ── niri close.glsl body ──
        float p = 1.0 - clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 100.0;

        float t = p * smokeSwirlSpeed + seed;

        float fluid = sm_warpedFbm(uv * smokeNoiseScale + seed, t);

        vec2 center = uv - 0.5;
        float dist = length(center * vec2(1.0, smokeVerticalSquish));

        float dissolve = (1.0 - dist) * 1.2 + fluid * 0.7;
        float remain = smoothstep(dissolve + 0.5, dissolve - 0.5, p * 1.8);

        float distort_strength = p * p * smokeDistortion;
        vec2 wq = vec2(sm_fbm(uv * 2.0 + vec2(0.0, t * 0.2)),
                       sm_fbm(uv * 2.0 + vec2(5.2, t * 0.2)));
        vec2 wr = vec2(sm_fbm(uv * 2.0 + 4.0 * wq + vec2(1.7, 9.2)),
                       sm_fbm(uv * 2.0 + 4.0 * wq + vec2(8.3, 2.8)));
        vec2 warped_uv = uv + (wr - 0.5) * distort_strength;

        // Soft inside-mask. The distortion above pushes UVs slightly past
        // [0, 1] at boundary fragments, and `uTexture0` is clamp-to-edge —
        // the typical edge alpha is 0 (window shadow / rounded corners) so
        // samples beyond the surface produce a grey-transparent border.
        // Fade to zero across a tight 0.005-wide band at each edge so the
        // warped silhouette crops cleanly. Same pattern as morph/plasma-flow.
        vec2 insideLo = smoothstep(vec2(0.0), vec2(0.005), warped_uv);
        vec2 insideHi = vec2(1.0) - smoothstep(vec2(0.995), vec2(1.0), warped_uv);
        float mask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;
        vec4 color = texture(uTexture0, warped_uv) * mask;

        float tail = smoothstep(1.0, 0.8, p);
        result = color * remain * tail;
    } else {
        // ── niri open.glsl body ──
        float p = clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 100.0;

        float t = p * smokeSwirlSpeed + seed;

        float fluid = sm_warpedFbm(uv * smokeNoiseScale + seed, t);

        vec2 center = uv - 0.5;
        float dist = length(center * vec2(1.0, smokeVerticalSquish));

        float appear = (1.0 - dist * 1.2) + (1.0 - fluid) * 0.7;
        float reveal = smoothstep(appear + 0.5, appear - 0.5, (1.0 - p) * 1.8);

        float distort_strength = (1.0 - p) * (1.0 - p) * (smokeDistortion * 0.875);
        vec2 wq = vec2(sm_fbm(uv * 2.0 + vec2(0.0, t * 0.2)),
                       sm_fbm(uv * 2.0 + vec2(5.2, t * 0.2)));
        vec2 wr = vec2(sm_fbm(uv * 2.0 + 4.0 * wq + vec2(1.7, 9.2)),
                       sm_fbm(uv * 2.0 + 4.0 * wq + vec2(8.3, 2.8)));
        vec2 warped_uv = uv + (wr - 0.5) * distort_strength;

        // Soft inside-mask. The distortion above pushes UVs slightly past
        // [0, 1] at boundary fragments, and `uTexture0` is clamp-to-edge —
        // the typical edge alpha is 0 (window shadow / rounded corners) so
        // samples beyond the surface produce a grey-transparent border.
        // Fade to zero across a tight 0.005-wide band at each edge so the
        // warped silhouette crops cleanly. Same pattern as morph/plasma-flow.
        vec2 insideLo = smoothstep(vec2(0.0), vec2(0.005), warped_uv);
        vec2 insideHi = vec2(1.0) - smoothstep(vec2(0.995), vec2(1.0), warped_uv);
        float mask = insideLo.x * insideLo.y * insideHi.x * insideHi.y;
        vec4 color = texture(uTexture0, warped_uv) * mask;

        result = color * reveal;
    }
    fragColor = result;
}
