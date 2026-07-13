// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Phosphor Scan — a Phosphor-set open/close candidate that leans into the
// name itself: the window powers on like a phosphor screen. A bright beam
// scans top to bottom, the rows it has passed glow with a decaying
// persistence trail before settling into the image, faint scanlines fade
// as the picture stabilises, and the beam carries the brand accent
// gradient (cyan #22D3EE → blue #3B82F6 → purple #A855F7 → rose #F43F5E).
// Below the beam the surface waits as a dark navy silhouette.
// Symmetric — the runtime flips iTime on the close leg, so open scans the
// picture in and close powers it back down through the same pass.
//
// Output is premultiplied alpha (phosphor-bloom's contract): the runtime
// hands pTransition's return straight to the compositor, so rgb is already
// scaled by alpha, plus an additive beam glow bounded by the surface
// coverage.

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
    // Power of the screen. iTime is per-leg [0,1] progress; use it directly
    // so the effect is symmetric — the runtime runs iTime 1->0 on the close
    // leg, scanning the picture back out through the same pass.
    float presence = clamp(iTime, 0.0, 1.0);

    // Endpoint early-outs: fully gone draws nothing; fully present hands
    // back the clean surface. Every transient below is beam-, mid-, or
    // dim-weighted, so both early-outs stay continuous.
    if (presence <= 0.0) return vec4(0.0);
    if (presence >= 1.0) return surfaceColor(uv);

    vec2 anchor = max(iAnchorSize, vec2(1.0));
    vec2 px = uv * anchor;

    // ── Beam position: sweeps top (0) to bottom (1), expanded past [0,1]
    // by its own half-height so the first and last rows fully clear the
    // beam band at the leg endpoints. ──
    float beamH = clamp(p_beamWidth, 0.03, 0.3);
    float yB = presence * (1.0 + 2.0 * beamH) - beamH;

    // Proximity to the beam centre (1 = on the beam).
    float bd = clamp(1.0 - abs(uv.y - yB) / beamH, 0.0, 1.0);

    // lit: 1 above the beam (already scanned), 0 below (still dark).
    float lit = smoothstep(yB + beamH * 0.5, yB - beamH * 0.5, uv.y);

    // Mid-leg envelope: peaks mid-flight, exactly zero at both endpoints.
    // Gates the persistence trail, scanlines, and jitter so a settled
    // screen is the plain surface with no residue.
    float mid = clamp(presence * (1.0 - presence) * 4.0, 0.0, 1.0);

    // ── Horizontal row jitter near the beam: the picture wobbles for a few
    // rows while the beam writes them, settling as it moves on. Per-row,
    // per-frame hash; boundary-masked so the displaced sample never smears
    // the clamped edge texel. ──
    float jitterAmp = clamp(p_jitter, 0.0, 1.0) * bd * mid * 4.0;
    float jx = (niriHash(vec2(floor(px.y), float(iFrame))) * 2.0 - 1.0)
             * jitterAmp / anchor.x;
    vec2 suv = vec2(uv.x + jx, uv.y);
    vec4 s = surfaceColor(suv) * boundaryMask(suv);   // premultiplied

    // ── Unlit silhouette below the beam: the dark screen, the window's own
    // luminance faintly showing through (phosphor-bloom's containment
    // shell). Fades in with presence so the open leg starts from nothing
    // and the close leg ends at nothing. ──
    vec3 tint = length(p_colorTint.rgb) > 0.01 ? p_colorTint.rgb : vec3(0.043, 0.090, 0.188);
    float lum  = dot(s.rgb, vec3(0.299, 0.587, 0.114));
    float lumN = (s.a > 0.001) ? lum / s.a : 0.0;
    float dim  = clamp(p_unlitDim, 0.0, 1.0) * smoothstep(0.0, 0.3, presence);

    float aheadA   = s.a * dim;
    vec3  aheadRGB = tint * (0.5 + 1.6 * lumN) * aheadA;

    vec3  rgb = mix(aheadRGB, s.rgb, lit);
    float a   = mix(aheadA, s.a, lit);

    // ── Scanlines on the lit picture, fading out as it stabilises: fine
    // alternating rows in device pixels, mid-gated so the settled image is
    // untouched. Multiplicative on the premultiplied pair, so coverage dims
    // in step with the body. ──
    float scanAmt = clamp(p_scanlines, 0.0, 1.0) * mid;
    float scan = 0.5 + 0.5 * sin(px.y * 3.14159265);
    float scanDim = 1.0 - scanAmt * 0.35 * (1.0 - scan) * lit;
    rgb *= scanDim;

    // ── Persistence trail: rows the beam has just written glow with the
    // gradient and decay toward the plain picture — the phosphor afterglow
    // that names the set. Coverage-weighted and mid-gated. ──
    float tail = 0.04 + clamp(p_persistence, 0.0, 1.0) * 0.30;
    float above = max(yB - uv.y, 0.0);
    float ag = (uv.y < yB) ? exp(-above / tail) : 0.0;
    vec3 agCol = fluxGradient(clamp(1.0 - ag * 0.5, 0.0, 1.0));
    rgb += agCol * ag * mid * s.a * 0.35;

    // ── The beam itself: a hot bar carrying the full gradient, cyan on the
    // leading (lower) edge and rose trailing into the written rows, with a
    // fine per-frame grain so it shimmers. Coverage-weighted so the light
    // stays inside the window silhouette. ──
    float q = clamp((uv.y - yB) / beamH * 0.5 + 0.5, 0.0, 1.0);
    vec3 beamCol = fluxGradient(1.0 - q);
    float sparkle = 0.85 + 0.30 * niriHash(floor(px / 2.0) + floor(float(iFrame) * 0.2));
    float beam = bd * bd * s.a * clamp(p_glow, 0.0, 2.0);
    rgb += beamCol * beam * sparkle * 0.9;

    // Same bounds as phosphor-bloom: bound the additive emissive here, and
    // never let a pixel be more opaque than the window is at that point.
    rgb = clamp(rgb, 0.0, 1.0);
    float outA = clamp(a + beam * 0.4, 0.0, s.a);

    return vec4(rgb, outA);
}
