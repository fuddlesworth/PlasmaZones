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
// smoothstep(...,(1-p)*1.8)`). This is a pIn/pOut pair: the harness
// feeds forward 0→1 `t` to both legs (so the niri `p` is just `t` in each
// branch) and dispatches the matching body by leg direction
// (`windowFadingIn`).
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline. niri's
// `niri_random_seed` is replaced by `surfaceSeed()` from `<noise.glsl>`.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl is pack-specific, so it stays here.
#include <noise.glsl>

// p_smokeNoiseScale / p_smokeSwirlSpeed / p_smokeVerticalSquish /
// p_smokeDistortion (customParams[0].xyzw) are generated from metadata.json.
// Both legs share the same params: `p_smokeDistortion` is the close-leg
// coefficient (default 0.4) and the open leg uses `* 0.875` to preserve the
// niri 0.4-vs-0.35 ratio so defaults reproduce the original visual exactly.

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

// `uv` is vTexCoord; `t` is the forward 0→1 leg progress (the harness applies
// legProgress()); `windowFadingIn` selects the niri open vs close body. The
// per-leg swirl time is `swirlT` (named to avoid shadowing the `t` progress).
vec4 smokeBody(vec2 uv, float t, bool windowFadingIn) {
    vec4 result;
    if (!windowFadingIn) {
        // ── niri close.glsl body (forward progress p = t) ──
        float p = t;
        float seed = surfaceSeed() * 100.0;

        float swirlT = p * p_smokeSwirlSpeed + seed;

        // `p_smokeNoiseScale` means "fbm cycles across the screen":
        // multiplying by iAnchorSize/iSurfaceScreenPos.zw scales the
        // cycle count to the fraction of the screen this surface
        // covers, so smoke feature pixel size stays constant across
        // popup vs. maximized windows. Matches niri's reference on
        // full-screen (multiplier = 1.0 there).
        vec2 screenScale = max(iAnchorSize, vec2(1.0)) / max(iSurfaceScreenPos.zw, vec2(1.0));
        vec2 perCardScale = p_smokeNoiseScale * screenScale;
        float fluid = sm_warpedFbm(uv * perCardScale + seed, swirlT);

        vec2 center = uv - 0.5;
        float dist = length(center * vec2(1.0, p_smokeVerticalSquish));

        float dissolve = (1.0 - dist) * 1.2 + fluid * 0.7;
        float remain = smoothstep(dissolve + 0.5, dissolve - 0.5, p * 1.8);

        // Secondary domain-warp fbm also feeds the screen-scale
        // multiplier so the displacement noise pattern (wq/wr) keeps a
        // constant feature pixel size — without it the `uv * 2.0` term
        // produces features that scale with the surface (one 2.0-cycle
        // pattern = 50% of card width regardless of pixels),
        // reintroducing the same Bug A the primary fbm fix above
        // resolved.
        float distort_strength = p * p * p_smokeDistortion;
        vec2 secondaryScale = 2.0 * screenScale;
        vec2 wq = vec2(sm_fbm(uv * secondaryScale + vec2(0.0, swirlT * 0.2)),
                       sm_fbm(uv * secondaryScale + vec2(5.2, swirlT * 0.2)));
        vec2 wr = vec2(sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(1.7, 9.2)),
                       sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(8.3, 2.8)));
        vec2 warped_uv = uv + (wr - 0.5) * distort_strength;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(warped_uv) * boundaryMask(warped_uv);

        float tail = smoothstep(1.0, 0.8, p);
        result = color * remain * tail;
    } else {
        // ── niri open.glsl body (forward progress p = t) ──
        float p = t;
        float seed = surfaceSeed() * 100.0;

        float swirlT = p * p_smokeSwirlSpeed + seed;

        // See close-branch comment above on the screen-anchored scaling.
        vec2 screenScale = max(iAnchorSize, vec2(1.0)) / max(iSurfaceScreenPos.zw, vec2(1.0));
        vec2 perCardScale = p_smokeNoiseScale * screenScale;
        float fluid = sm_warpedFbm(uv * perCardScale + seed, swirlT);

        vec2 center = uv - 0.5;
        float dist = length(center * vec2(1.0, p_smokeVerticalSquish));

        float appear = (1.0 - dist * 1.2) + (1.0 - fluid) * 0.7;
        float reveal = smoothstep(appear + 0.5, appear - 0.5, (1.0 - p) * 1.8);

        // See close-branch comment above on the secondary-warp scaling.
        float distort_strength = (1.0 - p) * (1.0 - p) * (p_smokeDistortion * 0.875);
        vec2 secondaryScale = 2.0 * screenScale;
        vec2 wq = vec2(sm_fbm(uv * secondaryScale + vec2(0.0, swirlT * 0.2)),
                       sm_fbm(uv * secondaryScale + vec2(5.2, swirlT * 0.2)));
        vec2 wr = vec2(sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(1.7, 9.2)),
                       sm_fbm(uv * secondaryScale + 4.0 * wq + vec2(8.3, 2.8)));
        vec2 warped_uv = uv + (wr - 0.5) * distort_strength;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(warped_uv) * boundaryMask(warped_uv);

        result = color * reveal;
    }
    return result;
}

vec4 pIn(vec2 uv, float t)  { return smokeBody(uv, t, true);  }
vec4 pOut(vec2 uv, float t) { return smokeBody(uv, t, false); }
