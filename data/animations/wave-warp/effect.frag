// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wave Warp transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/wave-warp).
// Soft directional wave wipe with scale-warp — the surface contracts
// inward as it sweeps across.
//
// Niri's wave-warp ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body with `niri_clamped_progress`
// translated to `clamp(iTime * frontSpeed, 0.0, 1.0)` and the runtime
// flip auto-mirrors the visual on close — no iIsReversed branch needed.
// (`frontSpeed` is a PlasmaZones addition; see the customParams block.)
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.

#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// `waveAngle` (radians) replaces niri's hardcoded `vec2 dir = (1, 0)`
// — single dial avoids the NaN risk of normalising a user-supplied
// vec2 that could come in as (0, 0).
// `frontSpeed` scales leg progress so the wave front can sweep faster
// than the leg: at 1.0 the niri timing is unchanged; above 1.0 the
// warp completes early and holds fully revealed for the rest of the
// leg. This subsumes the former standalone `crosswarp` shader, whose
// only dial was an equivalent front-speed control.
vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    // `frontSpeed` scales leg progress before the clamp; see the
    // customParams block above.
    float p = clamp(iTime * p_frontSpeed, 0.0, 1.0);

    // `(cos θ, sin θ)` is unit-length for any finite θ, so niri's
    // `normalize(dir)` is a no-op here — dropped. The `v /=
    // abs(v.x) + abs(v.y)` rescale below is the load-bearing
    // L1-normalisation that converts the unit direction into the
    // diagonal-aware wave coordinate space.
    vec2 dir = vec2(cos(p_waveAngle), sin(p_waveAngle));
    vec2 v = dir;
    v /= abs(v.x) + abs(v.y);
    float d = v.x * 0.5 + v.y * 0.5;
    float m = 1.0 - smoothstep(-p_waveSmoothness, 0.0, v.x * uv.x + v.y * uv.y - (d - 0.5 + p * (1.0 + p_waveSmoothness)));

    // The contraction is bounded: `m ∈ [0, 1]` (smoothstep-clamped) and
    // `uv ∈ [0, 1]` give `warped = (uv - 0.5) * m + 0.5 ∈ [0, 1]`, so
    // sampling `warped` cannot reach off-anchor texels. niri's original
    // `clamp(warped, 0, 1)` was a no-op for this reason and is dropped;
    // no boundary mask is needed either. The `m` multiply on the final
    // colour fades the contracted silhouette out as the wipe completes.
    vec2 warped = (uv - 0.5) * m + 0.5;
    return surfaceColor(warped) * m;
}
