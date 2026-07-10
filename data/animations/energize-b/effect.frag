// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Energize B — Star Trek transporter beam-up. A bright horizontal
// shower wave sweeps through the window early in the leg, leaving a
// trail of vertical beam-streaks and glimmering atom sparkles
// behind it. Visually inspired by the equivalent effect in
// Burn-My-Windows (energize-b.frag, Simon Schneegans), but written
// natively against our `iTime`/`uTexture0` contract: no
// `uForOpening` collapse, no compat layer.
//
// ## iTime convention
//
// SurfaceAnimator runs `iTime` 0→1 on show and 1→0 on hide. The
// shader is the entire visual transition. Each visual phase is a
// function of iTime alone with no direction branching:
//
//   • Window alpha is monotonic smootherstep across [0.20, 0.85].
//     Show ramps up, hide is the reverse.
//   • The shower wave's vertical position is `mix(-0.3, 1.3,
//     smoothstep(0.05, 0.30, iTime))` — top→bottom on show,
//     bottom→top on hide. Either reading is a valid sci-fi
//     transporter visual ("beam descends to deliver matter" /
//     "beam ascends, pulling matter up").
//   • The shower envelope is a Gaussian centred at iTime=0.18,
//     so it's brief and fires early on show / late on hide.
//   • Streaks and atom sparkles are gated by a sin-shaped energy
//     envelope peaking at iTime=0.5 — symmetric in both directions.
//
// ## Compositing
//
// `uTexture0` carries premultiplied alpha. We tint the window
// toward the beam colour as it fades (stronger than Energize A —
// 0.5 base mix vs A's 0.25 — to read as "matter is becoming
// energy"), then add three additive emission layers: shower band,
// streaks, atoms. All layers are tinted with the beam colour.

#include <noise.glsl>

// metadata.json declaration order:
//   color  → customColors[0]  (p_beamColor — only `.rgb` used; the effect
//                              drives its own emission alpha from the
//                              shower / streak / atom envelopes)
//   float  → customParams[0]  (p_particleScale)

// 2D simplex noise — MIT-licensed (Inigo Quilez,
// https://www.shadertoy.com/view/Msf3WH). Definitions in
// shared/noise.glsl.

vec4 pTransition(vec2 uv, float t)
{
    vec3 effectColor = p_beamColor.rgb;
    float pScale     = max(p_particleScale, 0.05);

    float v = clamp(t, 0.0, 1.0);

    // ----- Envelopes ------------------------------------------------------

    // Window alpha: smootherstep across [0.20, 0.85]. Wider hold than
    // Energize A (0.20→0.85 vs A's 0.15→0.85) so the beam-streaks
    // have visible airtime before the window starts re-solidifying
    // on show / has more presence after the shower passes on hide.
    float wt = clamp((v - 0.20) / 0.65, 0.0, 1.0);
    float windowAlpha = wt * wt * wt * (wt * (wt * 6.0 - 15.0) + 10.0);

    // Energy envelope drives streaks and atoms. sin(πv) peaks at
    // v=0.5; pow 0.55 broadens the peak more than Energize A so the
    // streaks linger across most of the leg.
    float energy = pow(sin(v * 3.14159265), 0.55);

    // Shower wave envelope: brief Gaussian peaking at v=0.18 so the
    // beam happens early on show / late on hide. Width σ≈0.08 keeps
    // it sharply timed.
    float showerT = (v - 0.18) / 0.08;
    float showerEnv = exp(-showerT * showerT);

    // ----- Spatial masks --------------------------------------------------

    // Window-edge fade so emission doesn't pop at the texture
    // boundary (~2.5% margin per side).
    vec2 edgeT     = uv * (1.0 - uv);
    float edgeFade = smoothstep(0.0, 0.025, edgeT.x) *
                     smoothstep(0.0, 0.025, edgeT.y);

    // Shower band: a horizontal wave whose vertical centre travels
    // through the window over the shower lifetime. The band's Y
    // position is monotone in iTime, so on show it descends and on
    // hide it ascends — both read as a transporter beam.
    float bandY    = mix(-0.3, 1.3, smoothstep(0.05, 0.30, v));
    float bandDist = abs(uv.y - bandY);
    // Band shape: bright core (12% surface height) with soft falloff
    // out to ~30%. smoothstep gives the soft-edged "neon tube" feel
    // rather than a hard line.
    float bandCore = smoothstep(0.30, 0.0, bandDist);
    float bandHot  = smoothstep(0.06, 0.0, bandDist);
    float showerMask = (bandCore * 0.6 + bandHot * 1.4) * showerEnv * edgeFade;

    // Atom mask: radial expanding disk gated by the energy envelope,
    // same pattern as Energize A. Provides the central "atom cloud"
    // region where fine sparkles are concentrated.
    float dist     = length(uv - 0.5) * 2.0;
    float atomDisk = smoothstep(1.4, 0.0, dist - energy * 1.4);
    float atomMask = atomDisk * energy * edgeFade;

    // Streak mask: covers the whole window during the energy phase.
    // Unlike Energize A's central concentration, beam-streaks span
    // the full window area for the "matter being pulled out in
    // beams" reading.
    float streakMask = energy * edgeFade;

    // ----- Particle field -------------------------------------------------

    // Match the defence-in-depth pattern in energize-a/effect.frag: even
    // though `pScale` is already clamped above (line 78), the
    // redundant max here makes a future refactor that drops the
    // source-side clamp safe. Without it, energize-a and energize-b
    // would diverge — energize-a still floors at the divide site,
    // energize-b would silently divide by zero.
    // Floor iResolution so a first-frame zero-sized surface doesn't
    // collapse pixelUV to (0,0) and flatten the entire particle field
    // for one paint. Matches the energize-a defence at the same site.
    vec2 pixelUV = (uv - 0.5) * resolutionSafe() / max(pScale, 0.05);

    // Shower band sparkles — added on top of the band itself for a
    // glittery beam edge. Fine cells, twinkle via animated offset.
    const float CELL_SHOWER = 4.0;  // very fine, dense
    vec2 showerUV     = pixelUV / CELL_SHOWER;
    vec2 showerOffset = vec2(iTime * 1.6, iTime * 0.8);
    float nShower     = simplex2D(showerUV + showerOffset);
    float sShower     = max(1.0 / max(1.0 - nShower, 0.001) - 1.0, 0.0);
    sShower = sShower * sShower;

    // Vertical beam streaks — extreme aspect ratio noise (15px wide
    // cells, very tall cells so vertical variation is minimal).
    // Output is smooth (not spike-shaped) for the gradient-stripe
    // feel; two octaves layered for fractal richness without being
    // too busy. The streak height tracks the surface so a 4K-tall
    // window covers roughly the same number of cells as a 1080p one
    // and the "minimal vertical variation" comment above stays true
    // regardless of surface size; a fixed 800 px would compress the
    // noise vertically on tall surfaces and wash it out.
    const float STREAK_WIDTH_PX = 15.0;
    float streakHeightPx = max(iResolution.y, 1.0) * 0.8;
    vec2 streakUV = vec2(
        pixelUV.x / STREAK_WIDTH_PX,
        pixelUV.y / streakHeightPx
    );
    streakUV.y += iTime * 0.4;  // slight vertical drift so streaks evolve
    float ns1 = simplex2D(streakUV);
    float ns2 = simplex2D(streakUV * 2.3 + 7.7);
    float streakNoise = ns1 * 0.7 + ns2 * 0.3;
    // Bias toward bright stripes only — most pixels emit ~0, the
    // brightest stripes saturate.
    float streakIntensity = pow(max(streakNoise - 0.3, 0.0) * 1.6, 2.0);

    // Atom sparkles — same fine 6px cells as Energize A, slightly
    // different offset rates so the two effects feel distinct
    // side-by-side.
    const float CELL_ATOM = 6.0;
    vec2 atomUV     = pixelUV / CELL_ATOM;
    vec2 atomOffset = vec2(iTime * 1.2, iTime * -1.0);
    float nAtom     = simplex2D(atomUV + atomOffset + 31.7);
    float sAtom     = max(1.0 / max(1.0 - nAtom, 0.001) - 1.0, 0.0);
    sAtom = sAtom * sAtom;

    // ----- Composite ------------------------------------------------------

    // Window: pre-multiplied texture, faded by alpha envelope, tinted
    // toward the beam colour while mid-fade. Stronger tint than
    // Energize A — at full window alpha tint is 0; at zero alpha
    // tint is 0.5 (matter "becomes energy"). Tint is computed on
    // pre-multiplied colour so transparent regions don't acquire a
    // colour halo.
    vec4 sampled = surfaceColor(uv);
    float tint = 0.5 * (1.0 - windowAlpha);
    sampled.rgb = mix(sampled.rgb, effectColor * sampled.a, tint);
    sampled *= windowAlpha;

    // Shower band emission — combined band glow + sparkle dust on
    // top. Bright, brief.
    float showerA = clamp(showerMask * (1.0 + 0.05 * sShower), 0.0, 1.0);
    vec3  showerRgb = effectColor * showerA;

    // Streak emission — long-running over the energy phase, smooth
    // gradient (no sharp peaks). Lower coefficient than shower
    // because streaks cover more of the window area.
    float streakA = clamp(streakIntensity * streakMask * 0.8, 0.0, 1.0);
    vec3  streakRgb = effectColor * streakA;

    // Atom sparkle emission — sharp peaks, sparse, central area.
    float atomA = clamp(0.018 * sAtom * atomMask, 0.0, 1.0);
    vec3  atomRgb = effectColor * atomA;

    // Sum all emissions additively over the window. Cap final alpha
    // so additive accumulation doesn't blow past 1.
    //
    // Every emission layer's RGB is `effectColor * <layer alpha>`, so
    // the layer-rgb sum is `effectColor * (showerA + streakA + atomA)`.
    // Computing it that way and clamping the *combined* alpha
    // contribution lets the RGB stay tied to the final alpha cap —
    // summing the per-layer rgb directly leaves rgb tracking the
    // unclamped alpha sum, so when the alphas saturate at 1.0 the
    // rgb keeps climbing past it. The result is fragColor.rgb >
    // fragColor.a (broken pre-multiplied invariant), which lights up
    // tinted backdrops as a halo — the same failure mode aura-glow
    // had before the premultiply-at-output fix.
    float totalEmitA   = clamp(showerA + streakA + atomA, 0.0, 1.0);
    vec3  totalEmitRgb = effectColor * totalEmitA;

    return vec4(
        sampled.rgb + totalEmitRgb,
        clamp(sampled.a + totalEmitA, 0.0, 1.0)
    );
}
