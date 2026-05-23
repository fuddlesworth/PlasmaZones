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

// Reference card edge length for pixel-constant `smokeNoiseScale`
// scaling. `smokeNoiseScale` is interpreted as "fbm noise cycles across
// an 800-pixel card"; the iAnchorSize multiply keeps swirl/curl pixel
// size constant on larger / smaller surfaces instead of stretching
// smoke features with the window.
const float kReferenceCardSize = 800.0;

float sm_fbm(vec2 p) {
    float v = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 6; i++) {
        v += amp * niriNoise(p);
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

        vec2 cardScale = max(iAnchorSize, vec2(1.0)) / kReferenceCardSize;
        vec2 perCardScale = smokeNoiseScale * cardScale;
        float fluid = sm_warpedFbm(uv * perCardScale + seed, t);

        vec2 center = uv - 0.5;
        float dist = length(center * vec2(1.0, smokeVerticalSquish));

        float dissolve = (1.0 - dist) * 1.2 + fluid * 0.7;
        float remain = smoothstep(dissolve + 0.5, dissolve - 0.5, p * 1.8);

        // Secondary domain-warp fbm also feeds the cardScale multiplier so
        // the displacement noise pattern (wq/wr) keeps a constant feature
        // pixel size — without it the `uv * 2.0` term produces features that
        // scale with the surface (one 2.0-cycle pattern = 50% of card width
        // regardless of pixels), reintroducing the same Bug A the primary
        // fbm fix above resolved.
        float distort_strength = p * p * smokeDistortion;
        vec2 secondaryScale = 2.0 * cardScale;
        vec2 wq = vec2(sm_fbm(uv * secondaryScale + vec2(0.0, t * 0.2)),
                       sm_fbm(uv * secondaryScale + vec2(5.2, t * 0.2)));
        vec2 wr = vec2(sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(1.7, 9.2)),
                       sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(8.3, 2.8)));
        vec2 warped_uv = uv + (wr - 0.5) * distort_strength;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(warped_uv) * boundaryMask(warped_uv);

        float tail = smoothstep(1.0, 0.8, p);
        result = color * remain * tail;
    } else {
        // ── niri open.glsl body ──
        float p = clamp(iTime, 0.0, 1.0);
        vec2 uv = vTexCoord;
        float seed = surfaceSeed() * 100.0;

        float t = p * smokeSwirlSpeed + seed;

        vec2 cardScale = max(iAnchorSize, vec2(1.0)) / kReferenceCardSize;
        vec2 perCardScale = smokeNoiseScale * cardScale;
        float fluid = sm_warpedFbm(uv * perCardScale + seed, t);

        vec2 center = uv - 0.5;
        float dist = length(center * vec2(1.0, smokeVerticalSquish));

        float appear = (1.0 - dist * 1.2) + (1.0 - fluid) * 0.7;
        float reveal = smoothstep(appear + 0.5, appear - 0.5, (1.0 - p) * 1.8);

        // See close-branch comment above on the secondary-warp scaling.
        float distort_strength = (1.0 - p) * (1.0 - p) * (smokeDistortion * 0.875);
        vec2 secondaryScale = 2.0 * cardScale;
        vec2 wq = vec2(sm_fbm(uv * secondaryScale + vec2(0.0, t * 0.2)),
                       sm_fbm(uv * secondaryScale + vec2(5.2, t * 0.2)));
        vec2 wr = vec2(sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(1.7, 9.2)),
                       sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(8.3, 2.8)));
        vec2 warped_uv = uv + (wr - 0.5) * distort_strength;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(warped_uv) * boundaryMask(warped_uv);

        result = color * reveal;
    }
    fragColor = result;
}
