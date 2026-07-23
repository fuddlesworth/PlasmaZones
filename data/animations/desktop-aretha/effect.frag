// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Aretha — a virtual-desktop switch in the Ghost-in-the-Shell / Aretha
// aesthetic (data/overlays/aretha-shell, data/animations/aretha-materialize).
// The outgoing desktop flips to the incoming one hexagon by hexagon along a
// diagonal data sweep: ahead of the sweep each hex cell still shows the
// outgoing desktop, behind it the incoming one, and on the materialising front
// the cells stutter with chromatic-aberration glitch behind a glowing cyan hex
// edge, with a pink corruption flash on high-hash cells. `t` is forward switch
// progress in [0,1].
//
// Colour is a real tunable here: `p_colorEdge` / `p_colorGlitch` resolve to the
// customColors pool, which the desktop-transition pass now binds at parity with
// the per-window and surface shader contracts (see DesktopTransitionManager).
#include <desktop_transition.glsl>
// classicHash + hexDist + hexLocal (from noise.glsl) and crossFade (from
// desktop_transition.glsl) replace this pack's pz_hash, pz_hexDist,
// pz_hexLocal, and pz_cross copies. The hex helpers are the same grid as
// aretha-materialize.
#include <noise.glsl>

vec4 pTransition(vec2 uv, float t) {
    // Hex cell identity in constant DEVICE pixels (iResolution is device-sized
    // on the desktop pass, so the grid is finer on a scaled output).
    float hexSize  = max(p_hexSize, 6.0);
    vec2  px       = uv * resolutionSafe();
    vec2  scaled   = px / hexSize;
    vec2  hex      = hexLocal(scaled);
    float d        = hexDist(hex);
    // Hash the cell's own center. A floor() of the center over the row pitch
    // collapses each base-lattice hex and its offset-lattice diagonal
    // neighbour onto one id, so adjacent pairs would pop and stutter in
    // lockstep; the raw center is unique per hex.
    vec2  cellId   = scaled - hex;
    float cellRand = classicHash(cellId);

    // Per-cell flip threshold: a diagonal data sweep blended with per-cell
    // random ordering. scatter=0 → clean diagonal wipe; 1 → fully scattered
    // hex pop-in.
    float grad      = (uv.x + uv.y) * 0.5;               // 0 top-left .. 1 bottom-right
    float threshold = mix(grad, cellRand, clamp(p_scatter, 0.0, 1.0));

    // Expand progress past [0,1] by the edge band so the first / last cells
    // fully clear their threshold band at t=0 and t=1 (clean endpoints).
    float band = 0.16;
    float p    = t * (1.0 + 2.0 * band) - band;

    // reveal: 0 = still outgoing desktop .. 1 = incoming desktop.
    float reveal = smoothstep(threshold - band, threshold + band, p);

    // Proximity to the materialising front (1 = on the leading edge).
    float edge = clamp(1.0 - abs((p - threshold) / band), 0.0, 1.0);

    float glitchAmt = clamp(p_glitch, 0.0, 1.0);

    // Per-cell stutter on the front: cells flicker between the two desktops as
    // they flip. Driven by iFrame (bound on the desktop pass at contract
    // parity) so the stutter is independent of the eased switch progress.
    float flick = classicHash(cellId + floor(float(iFrame) * 0.25));
    reveal = clamp(reveal + (flick - 0.5) * edge * glitchAmt * 0.6, 0.0, 1.0);

    // Chromatic-aberration split on the front, with per-cell horizontal jitter
    // so the tear is uneven across the sweep. Off the front the shift collapses
    // to zero, so the vast majority of fragments take a single crossfade tap.
    float split  = 0.012 * glitchAmt * edge;
    float jitter = (classicHash(cellId * 2.0 + floor(float(iFrame) * 0.1)) - 0.5) * glitchAmt * edge * 0.04;
    vec3  col;
    if (abs(split) + abs(jitter) < 1.0e-5) {
        col = crossFade(uv, reveal).rgb;
    } else {
        col.r = crossFade(uv + vec2(split + jitter, 0.0), reveal).r;
        col.g = crossFade(uv + vec2(jitter, 0.0), reveal).g;
        col.b = crossFade(uv + vec2(-split + jitter, 0.0), reveal).b;
    }

    // Scanline modulation on the front (CRT feel, constant DEVICE-pixel pitch).
    float scan = 0.85 + 0.15 * step(0.5, fract(px.y * 0.5));
    col *= mix(1.0, scan, glitchAmt * edge * 0.7);

    // Aretha hex-circuit leading edge: cyan hexagon outlines glow on cells at
    // the front, with a pink corruption flash on high-hash cells. Fall back to
    // the theme palette when a colour slot is unset (transparent black).
    vec3 cyan = length(p_colorEdge.rgb) > 0.01 ? p_colorEdge.rgb : vec3(0.333, 0.667, 1.000);
    vec3 pink = length(p_colorGlitch.rgb) > 0.01 ? p_colorGlitch.rgb : vec3(1.000, 0.333, 0.498);
    float hexLine   = smoothstep(0.46, 0.5, d);          // hexagon outline
    float frontGlow = edge * edge * clamp(p_edgeGlow, 0.0, 2.0);
    vec3  edgeCol   = mix(cyan, pink, step(0.7, cellRand) * 0.8);
    col += edgeCol * frontGlow * (0.35 + 0.65 * hexLine);

    // Two opaque desktops blended stay opaque — the pass draws with blending
    // off and replaces the screen, so alpha is a constant 1. No upper clamp:
    // the reveal blend is convex over the two captures, the scanline term is
    // multiplicative in (0, 1], and the edge glow only adds non-negative
    // colour — clamping would crush HDR capture values the blend never
    // created (the crush phosphor-peek's tail comment warns about).
    return vec4(col, 1.0);
}
