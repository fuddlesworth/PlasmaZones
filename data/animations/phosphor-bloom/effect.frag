// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Bloom — the official Phosphor brand window transition, the
// animation leg of the phosphor-flux / desktop-phosphor / border-phosphor
// set. Φ is the symbol for luminous flux, so the window fills with light:
// ahead of a diagonal sweep the surface is an unlit navy silhouette (the
// containment shell, present but dark), on the front a wide band of the
// brand accent gradient (cyan #22D3EE → blue #3B82F6 → purple #A855F7 →
// rose #F43F5E) carries the light in, and behind it the surface is lit.
// Symmetric — the runtime flips iTime on the close leg, so open floods the
// window with light and close drains it through the same pass.
//
// Output is premultiplied alpha (same contract as aretha-materialize): the
// runtime hands pTransition's return straight to the compositor, so rgb is
// already scaled by alpha, plus an additive gradient front glow bounded by
// the surface coverage.

#include <animation_uniforms.glsl>
#include <noise.glsl>

// Four-stop brand gradient, t in [0, 1]: cyan → blue → purple → rose.
vec3 fluxGradient(float t) {
    vec3 cyan   = length(p_colorCyan.rgb)   > 0.01 ? p_colorCyan.rgb   : vec3(0.133, 0.827, 0.933);
    vec3 blue   = length(p_colorBlue.rgb)   > 0.01 ? p_colorBlue.rgb   : vec3(0.231, 0.510, 0.965);
    vec3 purple = length(p_colorPurple.rgb) > 0.01 ? p_colorPurple.rgb : vec3(0.659, 0.333, 0.969);
    vec3 rose   = length(p_colorRose.rgb)   > 0.01 ? p_colorRose.rgb   : vec3(0.957, 0.247, 0.369);
    t = clamp(t, 0.0, 1.0) * 3.0;
    vec3 c = mix(cyan, blue, clamp(t, 0.0, 1.0));
    c = mix(c, purple, clamp(t - 1.0, 0.0, 1.0));
    c = mix(c, rose, clamp(t - 2.0, 0.0, 1.0));
    return c;
}

vec4 pTransition(vec2 uv, float t)
{
    // Presence of the light. iTime is per-leg [0,1] progress; use it directly
    // (not legProgress) so the effect is symmetric — the runtime runs iTime
    // 1->0 on the close leg, draining the window through the same sweep.
    float presence = clamp(iTime, 0.0, 1.0);

    // Endpoint early-outs: fully gone draws nothing; fully present hands back
    // the clean surface with no extra math.
    if (presence <= 0.0) return vec4(0.0);
    if (presence >= 1.0) return surfaceColor(uv);

    // ── Diagonal sweep coordinate with a gentle static ripple across the
    // perpendicular axis, so the light front reads as a liquid wavefront
    // rather than a ruled line. ──
    float band = clamp(p_bandWidth, 0.05, 0.6);
    float grad = (uv.x + uv.y) * 0.5;   // 0 top-left .. 1 bottom-right
    float perp = uv.x - uv.y;
    float ripple = sin(perp * 7.0) * 0.5 + sin(perp * 17.0 + 2.3) * 0.3;
    float threshold = grad + ripple * clamp(p_ripple, 0.0, 1.0) * 0.06;

    // Expand presence past [0,1] by the band so the first / last pixels fully
    // clear their threshold band at the leg endpoints.
    float p = presence * (1.0 + 2.0 * band) - band;

    // reveal: 0 ahead of the sweep (unlit) .. 1 behind it (lit).
    float reveal = smoothstep(threshold - band, threshold + band, p);

    // Proximity to the light front (1 = on the leading edge).
    float edge = clamp(1.0 - abs((p - threshold) / band), 0.0, 1.0);

    vec4 s = surfaceColor(uv);   // premultiplied

    // ── Unlit silhouette ahead of the front: the window as a dark navy
    // containment shell, its own luminance faintly showing through. Fades in
    // with presence so the open leg starts from nothing and the close leg
    // ends at nothing. ──
    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    float lum  = dot(s.rgb, vec3(0.299, 0.587, 0.114));
    float lumN = (s.a > 0.001) ? lum / s.a : 0.0;
    float dim  = clamp(p_unlitDim, 0.0, 1.0) * smoothstep(0.0, 0.3, presence);

    float aheadA   = s.a * dim;
    vec3  aheadRGB = tint * (0.5 + 1.6 * lumN) * aheadA;

    vec3  rgb = mix(aheadRGB, s.rgb, reveal);
    float a   = mix(aheadA, s.a, reveal);

    // ── Gradient light front: the full brand gradient rides the band, cyan
    // on the leading edge and rose trailing into the lit surface. A fine
    // per-frame grain makes the light shimmer as it passes. Coverage-weighted
    // by the surface's own alpha so the glow stays inside the window
    // silhouette and fades across the shadow margin. ──
    vec2 px = uv * max(iAnchorSize, vec2(1.0));
    float q = clamp((p - threshold) / band * 0.5 + 0.5, 0.0, 1.0);
    vec3 fluxC = fluxGradient(q);

    float sparkle = 0.85 + 0.30 * niriHash(floor(px / 2.0) + floor(float(iFrame) * 0.2));
    float glow = edge * edge * s.a * clamp(p_glow, 0.0, 2.0);

    rgb += fluxC * glow * sparkle * 0.9;

    // Same bounds as aretha-materialize: the harness writes this straight to
    // fragColor with no clamp pass, so bound the additive emissive here, and
    // never let a pixel be more opaque than the window is at that point.
    rgb = clamp(rgb, 0.0, 1.0);
    float outA = clamp(a + glow * 0.4, 0.0, s.a);

    return vec4(rgb, outA);
}
