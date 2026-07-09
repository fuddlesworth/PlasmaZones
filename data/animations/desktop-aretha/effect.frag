// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Aretha — a virtual-desktop switch in the Ghost-in-the-Shell / Aretha
// aesthetic (data/shaders/aretha-shell, data/animations/aretha-materialize).
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

#ifdef PLASMAZONES_KWIN
float pz_hash(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Hex helpers ported from data/shaders/aretha-shell (same grid as
// aretha-materialize): distance to cell centre and the nearest cell's local
// offset for a point in hex-grid space.
float pz_hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y); // 0 at centre .. ~0.5 at edge
}

vec2 pz_hexLocal(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

// Crossfade the two captured desktops at one uv.
vec4 pz_cross(vec2 uv, float m) {
    return mix(getFromColor(uv), getToColor(uv), m);
}
#endif // PLASMAZONES_KWIN

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    // Hex cell identity in constant screen pixels (independent of resolution).
    float hexSize  = max(p_hexSize, 6.0);
    vec2  px       = uv * max(iResolution, vec2(1.0));
    vec2  scaled   = px / hexSize;
    vec2  hex      = pz_hexLocal(scaled);
    float d        = pz_hexDist(hex);
    vec2  cellId   = floor((scaled - hex) / vec2(1.0, 1.732));
    float cellRand = pz_hash(cellId);

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
    float flick = pz_hash(cellId + floor(float(iFrame) * 0.25));
    reveal = clamp(reveal + (flick - 0.5) * edge * glitchAmt * 0.6, 0.0, 1.0);

    // Chromatic-aberration split on the front, with per-cell horizontal jitter
    // so the tear is uneven across the sweep. Off the front the shift collapses
    // to zero, so the vast majority of fragments take a single crossfade tap.
    float split  = 0.012 * glitchAmt * edge;
    float jitter = (pz_hash(cellId * 2.0 + floor(float(iFrame) * 0.1)) - 0.5) * glitchAmt * edge * 0.04;
    vec3  col;
    if (abs(split) + abs(jitter) < 1.0e-5) {
        col = pz_cross(uv, reveal).rgb;
    } else {
        col.r = pz_cross(uv + vec2(split + jitter, 0.0), reveal).r;
        col.g = pz_cross(uv + vec2(jitter, 0.0), reveal).g;
        col.b = pz_cross(uv + vec2(-split + jitter, 0.0), reveal).b;
    }

    // Scanline modulation on the front (CRT feel, constant pixel pitch).
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
    // off and replaces the screen, so alpha is a constant 1.
    return vec4(clamp(col, 0.0, 1.0), 1.0);
#else
    return vec4(0.0);
#endif
}
