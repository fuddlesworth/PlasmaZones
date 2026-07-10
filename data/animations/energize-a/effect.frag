// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Energize A — Star Trek transporter dematerialisation. The window
// dissolves into a shimmering shower of glowing particles with a
// concentrated central heart-glow that flares as the window
// disappears. Visually inspired by the equivalent effect in
// Burn-My-Windows (energize-a.frag, Simon Schneegans), but written
// natively against our `iTime`/`uTexture0` contract rather than
// translating their `uProgress`/`uForOpening` model.
//
// ## iTime convention
//
// SurfaceAnimator runs `iTime` 0→1 on show and 1→0 on hide; the
// shader is the entire visual transition. We make show and hide
// look correct by writing every envelope to be a function of
// `iTime` directly with no direction branching:
//
//   • Window alpha is monotonic in iTime (smootherstep across the
//     middle of the leg) — show fades the window in, hide fades it
//     out by the same curve played in reverse.
//   • The particle "energy" envelope is `sin(iTime·π)` — peaks at
//     iTime=0.5, zero at both endpoints. Particles bloom and
//     dissolve over the leg in the same arc whichever direction
//     iTime travels.
//   • The heart envelope is a Gaussian centred at iTime=0.2 with a
//     narrow width. On show this fires early (the moment the window
//     starts emerging); on hide this fires late (after the window
//     has mostly dematerialised). Both feel correct.
//
// ## Compositing
//
// `uTexture0` carries premultiplied alpha (Qt RHI / KWin
// convention). We tint the premultiplied window toward the effect
// colour as it fades, scale by the window-alpha envelope, and add
// particle emission additively — particles emit their own light
// over and around the window silhouette.

#include <noise.glsl>

// metadata.json declaration order:
//   color  → customColors[0]  (p_particleColor — only `.rgb` used; alpha
//                              ignored, the effect drives its own emission
//                              alpha from the particle envelopes below)
//   float  → customParams[0]  (p_particleScale)

// 2D simplex noise — MIT-licensed (Inigo Quilez,
// https://www.shadertoy.com/view/Msf3WH). Used for the per-pixel
// sparkle field; cheaper than 3D and the time animation is folded
// into the input UV instead of a third axis. Definitions in
// shared/noise.glsl.

vec4 pTransition(vec2 uv, float t)
{
    vec3 effectColor = p_particleColor.rgb;
    float pScale     = max(p_particleScale, 0.05);

    float v = clamp(t, 0.0, 1.0);

    // ----- Envelopes ------------------------------------------------------

    // Window alpha: smootherstep across [0.15, 0.85]. The window holds at
    // alpha 0 for the first ~15% of the leg, climbs through the middle
    // 70%, then settles at alpha 1 for the last ~15%. Smootherstep gives
    // an ease-in-out without the corners that pure smoothstep produces;
    // this is what makes the materialisation read as "ka-zhoosh" rather
    // than a linear fade.
    float wt = clamp((v - 0.15) / 0.7, 0.0, 1.0);
    float windowAlpha = wt * wt * wt * (wt * (wt * 6.0 - 15.0) + 10.0);

    // Energy envelope drives the atom shower — sin(πv) peaks at v=0.5
    // and is zero at both endpoints. pow 0.65 broadens the peak so the
    // shower lingers across mid-leg rather than spiking and immediately
    // collapsing. Raising this exponent toward 1 makes the shower more
    // peaked / momentary; lowering it toward 0.4 makes it linger longer.
    float energy = pow(sin(v * 3.14159265), 0.65);

    // Heart bloom — Gaussian centred at v=0.2, σ≈0.1. On show this
    // fires as the window first emerges; on hide this is the final
    // glow after the window has gone. The heart is what gives the
    // effect its "soul" reading rather than just a noise dissolve.
    float heartT = (v - 0.2) / 0.1;
    float heartEnv = exp(-heartT * heartT);

    // ----- Spatial masks --------------------------------------------------

    // Atom mask: a soft disk that grows from the centre outward driven
    // by `energy`. At energy=0 there's no mask anywhere (no particles
    // shown). At energy=1 the disk covers the entire window with a soft
    // falloff to corners.
    float dist     = length(uv - 0.5) * 2.0;        // 0 centre, ~1.41 corners
    float atomDisk = smoothstep(1.4, 0.0, dist - energy * 1.4);

    // Soft fade near the surface boundary so particles don't pop at the
    // texture edge. ~3% margin per side.
    vec2 edgeT     = uv * (1.0 - uv);
    float edgeFade = smoothstep(0.0, 0.025, edgeT.x) *
                     smoothstep(0.0, 0.025, edgeT.y);
    float atomMask = atomDisk * energy * edgeFade;

    // Heart shape: per-axis smoothstep-edge mask raised to a high
    // power. Each axis gives a ramp that's 1 in the middle 50% and
    // fades to 0 at the borders; the product is high only at the
    // window centre, and pow(., 5) sharpens that into a concentrated
    // central bloom rather than a soft halo.
    vec2 he = vec2(
        smoothstep(0.0, 1.0, clamp(uv.x * 2.0, 0.0, 1.0)) *
        smoothstep(0.0, 1.0, clamp((1.0 - uv.x) * 2.0, 0.0, 1.0)),
        smoothstep(0.0, 1.0, clamp(uv.y * 2.0, 0.0, 1.0)) *
        smoothstep(0.0, 1.0, clamp((1.0 - uv.y) * 2.0, 0.0, 1.0))
    );
    float heartShape = pow(he.x * he.y, 5.0) * 3.0;
    float heartMask  = clamp(heartShape * heartEnv, 0.0, 1.0);

    // ----- Particle field -------------------------------------------------

    // Sparkle UV is scaled by `iResolution` so cell size is in PIXELS,
    // not in normalised UV. Without this, sparkles balloon on large
    // windows and shrink on small ones — the previous version used a
    // fixed 8-cells-across multiplier, which made each sparkle
    // ~240 px on a 1920 px window. CELL_FINE / CELL_MED below are
    // pixel-space cell sizes; tweak these to control sparkle density.
    const float CELL_FINE = 6.0;
    const float CELL_MED  = 18.0;

    // Floor iResolution so a first-frame zero-sized surface doesn't
    // collapse pixelUV to (0,0) and flatten the entire sparkle field
    // for one paint. Matches the early-frame defence used by
    // pixelate/doom/honeycomb at the same divide site.
    vec2 pixelUV = (uv - 0.5) * resolutionSafe() / max(pScale, 0.05);

    // Layer A: dense fine sparkles — small cells, fast-evolving offset.
    // The 2D-offset trick (animating the lookup position via a slowly
    // varying vector) emulates 3D simplex's twinkle behaviour without
    // the extra noise cost. The offset itself moves continuously, so
    // sparkles fade in and out rather than translating across the
    // window.
    vec2 fineUV     = pixelUV / CELL_FINE;
    vec2 fineOffset = vec2(iTime * 1.4, iTime * -0.9);
    float nFine     = simplex2D(fineUV + fineOffset);
    float sFine     = max(1.0 / max(1.0 - nFine, 0.001) - 1.0, 0.0);
    sFine = sFine * sFine;

    // Layer B: medium-cell shimmer — fills in the bright "atom cloud"
    // feel between the fine sparkles. Slower offset so the cloud
    // breathes rather than sparkles.
    vec2 medUV     = pixelUV / CELL_MED;
    vec2 medOffset = vec2(iTime * 0.5, iTime * 0.3);
    float nMed     = simplex2D(medUV + medOffset + 17.31);
    float sMed     = max(1.0 / max(1.0 - nMed, 0.001) - 1.0, 0.0);
    sMed = sMed * sMed;

    // Combined sparkle intensity. Coefficients chosen so peaks read
    // bright but the field stays sparse — most pixels emit ~0 with
    // bright glints at noise peaks.
    float sparkles = clamp(0.018 * sFine + 0.012 * sMed, 0.0, 1.5);

    // ----- Composite ------------------------------------------------------

    // Window component: pre-multiplied texture, faded by the window
    // alpha envelope, tinted toward effect colour while it's mid-fade
    // (tint vanishes when fully solid). Tint is computed in
    // pre-multiplied space, so we scale the effect colour by the
    // sampled alpha to avoid the "halo of effect colour around
    // transparent regions" artefact that plain mix would produce.
    vec4 sampled = surfaceColor(uv);
    float tint = 0.25 * (1.0 - windowAlpha);
    sampled.rgb = mix(sampled.rgb, effectColor * sampled.a, tint);
    sampled *= windowAlpha;

    // Particle emission — additive light contribution. Particle alpha
    // is `sparkles * particleMask`, capped so additive RGB stays
    // sensible. Pre-multiplied output: `effectColor * particleA`.
    float particleMask = clamp(atomMask + heartMask, 0.0, 1.0);
    float particleA    = clamp(sparkles * particleMask, 0.0, 1.0);
    vec3  particleRgb  = effectColor * particleA;

    return vec4(
        sampled.rgb + particleRgb,
        clamp(sampled.a + particleA, 0.0, 1.0)
    );
}
