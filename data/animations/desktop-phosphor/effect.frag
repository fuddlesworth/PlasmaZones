// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Desktop Phosphor — the official Phosphor brand desktop switch, the
// desktop leg of the phosphor-flux / phosphor-bloom / border-phosphor set.
// A wide band of the brand accent gradient (cyan #22D3EE → blue #3B82F6 →
// purple #A855F7 → rose #F43F5E) sweeps diagonally across the screen: ahead
// of it the outgoing desktop, behind it the incoming one, and on the front
// the image bends softly through the light like refraction. `t` is forward
// switch progress in [0,1].
//
// Colour is a real tunable here: the p_color* slots resolve to the
// customColors pool, which the desktop-transition pass binds at parity with
// the per-window and surface shader contracts (see DesktopTransitionManager).
#include <desktop_transition.glsl>
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

vec4 pTransition(vec2 uv, float t) {
#ifdef PLASMAZONES_KWIN
    const float kPi = 3.14159265359;

    // ── Diagonal sweep coordinate with a gentle static ripple across the
    // perpendicular axis, matching the phosphor-bloom window front. ──
    float band = clamp(p_bandWidth, 0.05, 0.6);
    float grad = (uv.x + uv.y) * 0.5;   // 0 top-left .. 1 bottom-right
    float perp = uv.x - uv.y;
    float ripple = sin(perp * 6.0) * 0.5 + sin(perp * 15.0 + 1.9) * 0.3;
    float threshold = grad + ripple * clamp(p_ripple, 0.0, 1.0) * 0.05;

    // Expand progress past [0,1] by the band so the first / last pixels fully
    // clear their threshold band at t=0 and t=1 (clean endpoints).
    float p = t * (1.0 + 2.0 * band) - band;

    // reveal: 0 = still outgoing desktop .. 1 = incoming desktop.
    float reveal = smoothstep(threshold - band, threshold + band, p);

    // Proximity to the light front (1 = on the leading edge).
    float edge = clamp(1.0 - abs((p - threshold) / band), 0.0, 1.0);

    // ── Refraction: pixels near the front bend along the sweep direction, a
    // full sine cycle across the band so the offset is zero at the band
    // centre and at both band edges (no seams where the effect hands back the
    // untouched desktops). ──
    vec2 dir = normalize(vec2(1.0, 1.0));
    float bend = sin(clamp((p - threshold) / band, -1.0, 1.0) * kPi);
    vec2 offset = dir * bend * edge * clamp(p_warp, 0.0, 1.0) * 0.02;

    vec3 col = crossFade(uv + offset, reveal).rgb;

    // ── Gradient light front: the full brand gradient rides the band, cyan
    // on the leading edge and rose trailing into the incoming desktop, with
    // a fine per-frame grain so the light shimmers as it passes. iFrame is
    // bound on the desktop pass at contract parity, independent of the eased
    // switch progress. ──
    float q = clamp((p - threshold) / band * 0.5 + 0.5, 0.0, 1.0);
    vec3 fluxC = fluxGradient(q);
    float sparkle = 0.9 + 0.2 * classicHash(floor(uv * resolutionSafe() / 2.0)
                                            + floor(float(iFrame) * 0.2));
    col += fluxC * edge * edge * clamp(p_glow, 0.0, 2.0) * 0.55 * sparkle;

    // Two opaque desktops blended stay opaque — the pass draws with blending
    // off and replaces the screen, so alpha is a constant 1.
    return vec4(clamp(col, 0.0, 1.0), 1.0);
#else
    return vec4(0.0);
#endif
}
