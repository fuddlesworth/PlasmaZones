// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Wave Warp transition — a soft directional wave wipe with scale-warp
// where the surface contracts inward as it sweeps across. Inspired by
// liixini/shaders' niri wave-warp shader.
//
// Symmetric transition, written as a single `pTransition`. The runtime
// flips the leg's iTime on reverse legs (0→1 on open, 1→0 on close), so
// the reveal reads `clamp(iTime * frontSpeed, 0.0, 1.0)` directly and the
// close leg plays in reverse automatically, with no `iIsReversed` branch.
// (`frontSpeed` is a PlasmaZones addition; see the customParams block.)
// Geometry and texture coordinates coincide here, so
// `texture(uTexture0, uv)` samples directly.

#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots.
// `waveAngle` (radians) sets the sweep direction as a single dial,
// avoiding the NaN risk of normalising a user-supplied vec2 that could
// come in as (0, 0).
// `frontSpeed` scales leg progress so the wave front can sweep faster
// than the leg: at 1.0 the timing is the plain leg pace; above 1.0 the
// warp completes early and holds fully revealed for the rest of the
// leg. This subsumes the former standalone `crosswarp` shader, whose
// only dial was an equivalent front-speed control.
vec4 pTransition(vec2 uv, float t) {
    // `frontSpeed` scales leg progress before the clamp; see the
    // customParams block above.
    float p = clamp(iTime * p_frontSpeed, 0.0, 1.0);

    // `(cos θ, sin θ)` is unit-length for any finite θ, so a
    // `normalize(dir)` would be a no-op here and is omitted. The `v /=
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
    // sampling `warped` cannot reach off-anchor texels. A
    // `clamp(warped, 0, 1)` would be a no-op for this reason and is
    // omitted; no boundary mask is needed either. The `m` multiply on the final
    // colour fades the contracted silhouette out as the wipe completes.
    vec2 warped = (uv - 0.5) * m + 0.5;
    return surfaceColor(warped) * m;
}
