// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Inkwell Drop transition — ported from liixini/shaders niri shader
// (https://github.com/liixini/shaders/tree/main/inkwell-drop). Drop-
// and-ripple — a circular ink front spreads from impact with concentric
// ringed distortion.
//
// Niri's inkwell-drop ships symmetric close.glsl/open.glsl. PlasmaZones'
// runtime flips iTime on reverse legs (1→0 on close, 0→1 on open),
// so we use the niri OPEN body with `niri_clamped_progress` translated
// to `clamp(iTime, 0.0, 1.0)` and the runtime flip auto-mirrors the
// visual on close — no iIsReversed branch needed.
//
// niri's `niri_geo_to_tex` is the identity mat3 in PlasmaZones (geometry
// == texture coords here), so the matrix multiply is dropped and
// `texture(uTexture0, uv)` samples directly. `texture2D` (GLSL ES) is
// rewritten to `texture` (GLSL 4.50 core) inline.
//
// One deviation from the niri body: niri samples the ripple-distorted
// coord through `clamp(uv + dir * ripple, 0.0, 1.0)`. Pinning off-window
// UVs to the edge texel via clamp-to-edge smears a border fringe around
// the ripple front, so we drop the clamp and crop the sampled texels
// via boundaryMask (see noise.glsl) instead — off-surface stays
// transparent, no edge-pixel bleed. Same fix as wave-warp / soft-warp-fade.

#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
    // ── niri OPEN body (handles both legs via runtime iTime flip) ──
    float p = clamp(t, 0.0, 1.0);

    vec2 impact = vec2(p_impactX, p_impactY);
    vec2 c = uv - impact;
    // Aspect-correct horizontal distance so `length(c)` is pixel-isotropic
    // (the impact point reads as a true circle, not an ellipse). Use
    // `iResolution` — the rect that `vTexCoord` actually spans — rather
    // than `iAnchorSize`. On the daemon's anchor-extent path the two are
    // identical (FBO covers the anchor 1:1). On KWin's anchor-extent path
    // the rasterised quad covers the EXPANDED rect (frame + decoration
    // shadow), so `vTexCoord ∈ [0,1]` is over the expanded rect, and the
    // aspect ratio that makes length(c) circular in pixel space is the
    // expanded rect's, not the bare frame's. Mismatch was a slight
    // ellipticity scaling with shadow padding.
    c.x *= iResolution.x / max(iResolution.y, 0.0001);
    float d = length(c);
    float front = p * p_frontSpeed;
    float ring1 = sin((d - front) * 80.0) * exp(-abs(d - front) * 6.0);
    float ring2 = sin((d - front + 0.08) * 80.0) * exp(-abs(d - front + 0.08) * 8.0) * 0.6;
    float ring3 = sin((d - front + 0.16) * 80.0) * exp(-abs(d - front + 0.16) * 10.0) * 0.4;
    float ripple = (ring1 + ring2 + ring3) * p_rippleStrength * (1.0 - p * 0.5);
    vec2 dir = (d > 0.001) ? normalize(c) : vec2(0.0);
    vec2 distorted = uv + dir * ripple;

    // boundaryMask: see noise.glsl. Crops off-window samples to transparent
    // — `distorted` is `uv + dir * ripple` where `ripple` can swing past the
    // anchor edges as the ring sweeps, so the mask is load-bearing here.
    vec4 win = surfaceColor(distorted) * boundaryMask(distorted);

    float reveal = smoothstep(0.05, -0.02, d - front);
    return win * reveal;
}
