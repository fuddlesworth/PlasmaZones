// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Fade transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/fade). Gentle scale-
// and-fade transition. Open eases scale 0.95→1.0 while alpha ramps
// smoothstep(revealStart, revealEnd, p); close mirrors both curves.
//
// Deviation from niri: revealEnd DEFAULTS to 1.0 where niri hardcodes
// 0.8. smoothstep(0, 0.8) pins alpha at 1 for the last 20% of the open
// leg (and the first 20% of the close leg) while the only other motion
// is the final 1% of the scale ease — an imperceptible dead band that
// an eased progress curve stretches into a hang (the phosphor-peek
// dead-domain bug). Dial revealEnd back to 0.8 for the exact niri
// timing.
//
// Niri's fade ships separate close.glsl/open.glsl bodies — open uses
// `mix(0.95, 1.0, p)` for scale and `smoothstep(0.0, 0.8, p)` for
// alpha, close `mix(1.0, 0.95, p)` and the reversed-edge
// `smoothstep(1.0, 0.2, p)` (expressed below in the inverted-safe
// form). Algebraically close(p) == open(1 − p) for BOTH curves and
// every parameter choice (substitute q = 1 − p in either smoothstep
// argument and the clamp terms are identical), so the pair is a pure
// time-reversal. We still branch on `windowFadingIn` as a `pIn`/`pOut`
// pair to mirror niri's two shipped files one-to-one, keeping each
// leg's body diffable against its upstream original.
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
// open vs close body. The legs are a pure time-reversal of each other
// (see the header), but the split keeps each body diffable against its
// niri file, so the direction is threaded in rather than left to an
// iTime flip.
vec4 fadeBody(vec2 uv, float t, bool windowFadingIn) {
    vec4 result;
    if (!windowFadingIn) {
        // ── niri close.glsl body ──
        // close-leg p = niri_clamped_progress; the harness un-flips iTime
        // to absolute leg progress in [0,1], which is `t`.
        float p = t;

        vec2 center = vec2(0.5, 0.5);
        float scale = mix(1.0, 1.0 - p_scaleAmount, p);
        // Floor the divisor. `scale` rides the RAW leg progress, which may leave
        // [0,1] (an overshooting curve delivers its overshoot) but is bounded by the
        // engine to the envelope [-1, 2]. With metadata.json capping p_scaleAmount
        // at 0.3, scale stays inside [0.4, 1.3] across the whole envelope — it
        // crosses zero only at p = 1/p_scaleAmount > 3.3, outside anything a curve
        // can deliver. The max() is defence in depth: raise the p_scaleAmount cap
        // past 1.0 (or widen the envelope) and without it this becomes a
        // divide-by-zero, a NaN, and a black window.
        vec2 scaled_uv = (uv - center) / max(scale, 0.05) + center;

        // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
        vec4 color = surfaceColor(scaled_uv) * boundaryMask(scaled_uv);

        // Inverted form rather than swapped arguments: smoothstep is
        // undefined when edge0 >= edge1 per the GLSL spec, and niri's
        // literal `smoothstep(1.0 - rS, 1.0 - rE, p)` is always reversed
        // (rS caps at 0.49, rE floors at 0.51). Value-identical on a
        // conformant driver: 1 - S(u) == S(1 - u).
        float alpha = 1.0 - smoothstep(1.0 - p_revealEnd, 1.0 - p_revealStart, p);

        result = color * alpha;
    } else {
        // ── niri open.glsl body ──
        // open-leg p = niri_clamped_progress; the harness feeds forward
        // 0→1 leg progress, which is `t`.
        float p = t;

        vec2 center = vec2(0.5, 0.5);
        float scale = mix(1.0 - p_scaleAmount, 1.0, p);
        // Floor the divisor. `scale` rides the RAW leg progress, which may leave
        // [0,1] (an overshooting curve delivers its overshoot) but is bounded by the
        // engine to the envelope [-1, 2]. With metadata.json capping p_scaleAmount
        // at 0.3, this leg's scale stays inside [0.4, 1.3] across the whole
        // envelope — it crosses zero only at p = -(1 - p_scaleAmount)/p_scaleAmount
        // ≈ -2.33, past the floor of anything a curve can deliver. The max() is
        // defence in depth: raise the p_scaleAmount cap past 1.0 (or widen the
        // envelope) and without it this becomes a divide-by-zero, a NaN, and a
        // black window.
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
