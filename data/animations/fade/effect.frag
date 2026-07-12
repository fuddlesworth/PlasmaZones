// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fade transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/fade). Gentle scale-
// and-fade transition. Open eases scale 0.95→1.0 with smoothstep(0,0.8)
// fade-in; close eases scale 1.0→0.95 with smoothstep(1.0,0.2)
// fade-out.
//
// Niri's fade ships GENUINELY ASYMMETRIC close.glsl/open.glsl — open
// uses `mix(0.95, 1.0, p)` for scale and `smoothstep(0.0, 0.8, p)` for
// alpha, while close uses `mix(1.0, 0.95, p)` for scale and
// `smoothstep(1.0, 0.2, p)` for alpha. These are different curves,
// not a simple time-reversal, so the iTime flip alone can't express
// both legs. We branch on `windowFadingIn` (open vs close leg) to select
// the correct body, exposed as a `pIn`/`pOut` pair.
//
// Per the entry-point contract, the harness un-flips iTime to an
// ABSOLUTE forward leg progress `t` in [0,1] running 0→1 on BOTH legs
// (the old close-leg `(1.0 - clamp(iTime, 0.0, 1.0))` translation
// produced exactly this). Both niri bodies were authored to consume
// that absolute leg progress, so `p` is simply `t` in each branch.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out,
// and main(). noise.glsl (boundaryMask) is pack-specific, so it stays here.
#include <noise.glsl>

// p_scaleAmount / p_revealStart / p_revealEnd (customParams[0].xyz) are
// generated from metadata.json — no hand-written slot #defines.

// Shared body for both legs. `t` is forward 0→1 leg progress (the harness
// un-flipped iTime via legProgress()); `windowFadingIn` selects the niri
// open vs close body — these legs are GENUINELY ASYMMETRIC (different
// scale/alpha curves), not a simple time-reversal, so the direction is
// threaded in rather than left to an iTime flip.
vec4 fadeBody(vec2 uv, float t, bool windowFadingIn) {
    vec4 result;
    if (!windowFadingIn) {
        // ── niri close.glsl body ──
        // close-leg p = niri_clamped_progress; the harness un-flips iTime
        // to absolute leg progress in [0,1], which is `t`.
        float p = t;

        vec2 center = vec2(0.5, 0.5);
        float scale = mix(1.0, 1.0 - p_scaleAmount, p);
        // Floor the divisor. `scale` rides the RAW leg progress, which is no longer
        // bounded to [0,1] (an overshooting curve delivers its overshoot), so this is
        // safe today only because metadata.json caps p_scaleAmount at 0.3 — scale
        // would not reach 0 until a 3.3x overshoot. Do not rely on that coincidence:
        // raise the cap or widen the envelope and this becomes a divide-by-zero, a
        // NaN, and a black window.
        vec2 scaled_uv = (uv - center) / max(scale, 0.05) + center;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(scaled_uv) * boundaryMask(scaled_uv);

        float alpha = smoothstep(1.0 - p_revealStart, 1.0 - p_revealEnd, p);

        result = color * alpha;
    } else {
        // ── niri open.glsl body ──
        // open-leg p = niri_clamped_progress; the harness feeds forward
        // 0→1 leg progress, which is `t`.
        float p = t;

        vec2 center = vec2(0.5, 0.5);
        float scale = mix(1.0 - p_scaleAmount, 1.0, p);
        // Floor the divisor. `scale` rides the RAW leg progress, which is no longer
        // bounded to [0,1] (an overshooting curve delivers its overshoot), so this is
        // safe today only because metadata.json caps p_scaleAmount at 0.3 — scale
        // would not reach 0 until a 3.3x overshoot. Do not rely on that coincidence:
        // raise the cap or widen the envelope and this becomes a divide-by-zero, a
        // NaN, and a black window.
        vec2 scaled_uv = (uv - center) / max(scale, 0.05) + center;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(scaled_uv) * boundaryMask(scaled_uv);

        float alpha = smoothstep(p_revealStart, p_revealEnd, p);

        result = color * alpha;
    }
    return result;
}

vec4 pIn(vec2 uv, float t)  { return fadeBody(uv, t, true);  }
vec4 pOut(vec2 uv, float t) { return fadeBody(uv, t, false); }
