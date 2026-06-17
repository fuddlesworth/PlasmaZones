// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Aretha Materialize — cyberpunk hex-grid window transition matching the
// data/shaders/aretha-shell aesthetic (Ghost-in-the-Shell hex grid + cyan /
// teal / pink data glitch). The window materialises hexagon-by-hexagon along
// a diagonal data sweep: ahead of the sweep the surface is dissolved into
// flickering hex cells with chromatic-aberration glitch behind a bright cyan
// leading edge; behind it the surface resolves cleanly. Symmetric — the
// runtime flips iTime on the close leg, so open builds the window up and
// close tears it down through the same materialise pass.
//
// Output is premultiplied alpha (matches the glitch transition's contract):
// the runtime hands pTransition's return value straight to the compositor
// without re-premultiplying, so this returns rgb already scaled by alpha,
// plus an additive cyan front glow (rgb > alpha is intentional emissive).

#include <animation_uniforms.glsl>
#include <noise.glsl>

// ── Hex helpers (ported from data/shaders/aretha-shell) ────────────────────
float hexDist(vec2 p) {
    p = abs(p);
    return max(p.x * 0.866025 + p.y * 0.5, p.y);  // 0 at center .. ~0.5 at edge
}

vec2 hexLocal(vec2 uv) {
    vec2 r = vec2(1.0, 1.732);
    vec2 h = r * 0.5;
    vec2 a = mod(uv, r) - h;
    vec2 b = mod(uv - h, r) - h;
    return dot(a, a) < dot(b, b) ? a : b;
}

vec4 pTransition(vec2 uv, float t)
{
    // Presence of the window. iTime is per-leg [0,1] progress; use it
    // directly (not legProgress) so the effect is symmetric — the runtime
    // runs iTime 1->0 on the close leg, tearing the window down through the
    // same materialise pass that built it up on open.
    float presence = clamp(iTime, 0.0, 1.0);

    // Endpoint early-outs (mirror glitch): fully gone draws nothing; fully
    // present hands back the clean surface with no per-cell math or extra
    // texture taps.
    if (presence <= 0.0) return vec4(0.0);
    if (presence >= 1.0) return surfaceColor(uv);

    // ── Hex cell identity in constant pixel size (independent of window
    // size — iAnchorSize is the card's pixel dimensions). ──
    float hexSize = max(p_hexSize, 4.0);
    vec2 px       = uv * max(iAnchorSize, vec2(1.0));
    vec2 scaled   = px / hexSize;
    vec2 hex      = hexLocal(scaled);
    float d       = hexDist(hex);
    vec2 cellId   = floor((scaled - hex) / vec2(1.0, 1.732));
    float cellRand = niriHash(cellId);

    // ── Per-cell reveal threshold: diagonal data sweep blended with random
    // per-cell ordering. scatter=0 → clean diagonal wipe; 1 → fully
    // scattered hex pop-in. ──
    float grad      = (uv.x + uv.y) * 0.5;   // 0 top-left .. 1 bottom-right
    float threshold = mix(grad, cellRand, clamp(p_randomness, 0.0, 1.0));

    // Expand presence past [0,1] by the edge band so the first / last cells
    // fully clear their threshold band at the leg endpoints.
    float band = 0.18;
    float p    = presence * (1.0 + 2.0 * band) - band;

    // reveal: 0 ahead of the sweep (dissolved) .. 1 behind it (resolved).
    float reveal = smoothstep(threshold - band, threshold + band, p);

    // Proximity to the materialising front (1 = on the leading edge).
    float edge = clamp(1.0 - abs((p - threshold) / band), 0.0, 1.0);

    // Per-cell flicker on the front: cells stutter as they snap in. Driven by
    // iFrame (monotonic on BOTH legs — see glitch) so the reverse leg gets a
    // fresh stutter rather than a reverse replay.
    float flick        = niriHash(cellId + floor(float(iFrame) * 0.25));
    float frontFlicker = mix(1.0, 0.35 + 0.65 * flick, edge);

    // ── Chromatic-aberration glitch on the front (same un-premultiply /
    // re-premultiply technique as the glitch transition). ──
    float glitchAmt = clamp(p_glitch, 0.0, 1.0) * edge;
    float split     = p_rgbSplit * glitchAmt;
    // Per-cell horizontal jitter so the split tears unevenly across the front.
    float jitter = (niriHash(cellId * 2.0 + floor(float(iFrame) * 0.1)) - 0.5)
                   * glitchAmt * 0.08;

    float a;
    vec3 surf;  // un-premultiplied window color
    // Off the materialising front the per-channel shift collapses to zero, so
    // all three taps resolve to the same UV. Skip the redundant three-tap (and
    // the two divides) on those fragments — the vast majority every frame —
    // and take a single sample, mirroring the glitch transition's early-out.
    if (abs(split) + abs(jitter) < 1e-5) {
        vec4 s = surfaceColor(uv);
        a    = s.a;
        surf = (s.a > 0.001) ? s.rgb / s.a : vec3(0.0);
    } else {
        vec2 uvR = uv + vec2(split + jitter, 0.0);
        vec2 uvG = uv + vec2(jitter, 0.0);
        vec2 uvB = uv + vec2(-split + jitter, 0.0);
        vec4 sR = surfaceColor(uvR);
        vec4 sG = surfaceColor(uvG);
        vec4 sB = surfaceColor(uvB);
        a = max(max(sR.a, sG.a), sB.a);
        float r = (sR.a > 0.001) ? sR.r / sR.a : 0.0;
        float g = (sG.a > 0.001) ? sG.g / sG.a : 0.0;
        float b = (sB.a > 0.001) ? sB.b / sB.a : 0.0;
        surf = vec3(r, g, b);
    }

    // ── Scanline modulation (CRT feel, constant pixel pitch). ──
    float scan = 0.85 + 0.15 * step(0.5, fract(px.y * 0.5));
    surf *= mix(1.0, scan, glitchAmt * 0.7);

    // Window contribution, faded in by reveal * front flicker, re-premultiplied.
    float surfA = a * reveal * frontFlicker;
    vec3 color  = surf * surfA;

    // ── Aretha hex-circuit leading edge: cyan hexagon outlines glow on cells
    // currently materialising (the front), fading as they resolve. Cells with
    // a high hash flash pink for the GitS "data corruption" feel. ──
    vec3 cyan = length(p_colorEdge.rgb) > 0.01 ? p_colorEdge.rgb : vec3(0.333, 0.667, 1.000);
    vec3 pink = length(p_colorGlitch.rgb) > 0.01 ? p_colorGlitch.rgb : vec3(1.000, 0.333, 0.498);
    float edgeGlow = clamp(p_edgeGlow, 0.0, 2.0);

    float hexLine = smoothstep(0.46, 0.5, d);  // hexagon outline
    // Coverage-weight the glow by the surface's own alpha `a` instead of a
    // hard `step(0.001, a)`. The hard step lit the drop-shadow / anti-aliased
    // margin — anything with the faintest alpha — at FULL intensity, so the
    // cyan front filled the whole surface rect rather than the window
    // silhouette. Multiplying by `a` (the same way dissolve/fade gate purely
    // on surface alpha) keeps the glow inside the visible window and fades it
    // out across the shadow.
    float frontGlow = edge * edge * a * edgeGlow;  // peaks on the front
    vec3  edgeCol   = mix(cyan, pink, step(0.7, cellRand) * 0.8);

    color += edgeCol * frontGlow * (0.35 + 0.65 * hexLine);
    // The animation harness writes pTransition's return straight to fragColor
    // with no clampFragColor pass (the overlay path has one; this one does
    // not — see animationshaderregistry.cpp transitionMain). Bound the additive
    // emissive glow here so a bright window front doesn't push rgb past
    // premultiplied range and clip to white / over-expose on the compositor.
    color = clamp(color, 0.0, 1.0);
    // Clamp output alpha to the surface coverage `a`: a pixel is never more
    // opaque than the window is at that point, so transparent / shadow regions
    // stay transparent and the effect only ever covers what's visible.
    float outA = clamp(surfA + frontGlow * (0.25 + 0.40 * hexLine), 0.0, a);

    return vec4(color, outA);
}
