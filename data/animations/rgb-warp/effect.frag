// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// RGB Warp — vertical chromatic-aberration ripple. The window
// content lifts off the top of the surface, with each colour
// channel travelling at a slightly different speed so a rainbow
// trail follows the lift. Visually inspired by Burn-My-Windows
// (rgbwarp.frag, Justin Garza + Simon Schneegans), written
// natively against our `iTime`/`uTexture0` contract.
//
// ## iTime convention
//
// `progress = 1 - clamp(iTime, 0, 1)` — 0 at visible, 1 at
// destroyed. On hide (iTime 1→0) the window rises off the top.
// On show (iTime 0→1) it falls into place from above. No
// direction branch.
//
// ## Wave timing
//
// A vertical wave sweeps top→bottom over a fraction `waveTime` of
// the timeline (`waveTime = mix(0.1, 0.9, minSpeed)`). Pixels
// above the wavefront receive a non-zero offset; the offset is
// normalised so the maximum y-shift at full progress is one
// surface height. Per-channel multipliers `(speed - minSpeed + 1)`
// give the slowest channel a multiplier of 1.0 and faster channels
// progressively higher multipliers, so faster channels appear to
// trail higher up the window.
//
// ## Sampling and brightness
//
// Each colour channel is sampled at a different y-offset; output
// is recombined per-channel. A `FadeInOut` brightness pulse peaks
// at progress=0.5 (mid-leg). An edge fade keeps the rising-content
// reading clean at the surface boundary.

#include <noise.glsl>

// Bell curve in [0, 1], peaks at t=0.5. `power` controls steepness.
// `pow(x, y)` is undefined per GLSL 4.50 spec when x < 0; the input
// `(t - 0.5) / 0.5` is negative for the first half of every leg, so
// strict drivers (NVIDIA) return NaN and corrupt fragColor for half
// the animation. `abs()` makes the base non-negative — safe for any
// power, even or odd, on every conformant driver.
float fadeInOut(float t, float power)
{
    float s = -1.0 * pow(abs((t - 0.5) / 0.5), power) + 1.0;
    return clamp(s, 0.0, 1.0);
}

vec4 pTransition(vec2 uv, float t)
{
    float visibility = clamp(t, 0.0, 1.0);
    float progress   = 1.0 - visibility;

    // The wave sweeps faster when minSpeed is small (wave finishes
    // earlier in the timeline). minSpeed clamped above 0 AND below the
    // value that drives waveTime ≥ 1.0, so the offset normalisation
    // below has a structurally safe denominator without depending on
    // its own per-divide floor to cancel out a brittle (1/wt - 1) ≈ 0
    // edge case. With minSpeed ≤ 1.0, `mix(0.1, 0.9, minSpeed)` ≤ 0.9,
    // leaving `(1/0.9) - 1 ≈ 0.111` as the smallest reachable
    // denominator — safe by construction.
    // Clamp each channel speed individually FIRST so a host that bypasses
    // metadata pushes ALL three below 0.05 doesn't produce negative
    // multipliers below. Without this individual clamp, e.g. p_speedR=0.01
    // with floored minSpeed=0.05 yields mulR = max(0.01 - 0.05 + 1, 0) =
    // 0.96 — a downward multiplier (channel trails DOWN, not up) that
    // violates the "slowest channel gets ×1, faster channels higher"
    // contract.
    float speedRClamped = clamp(p_speedR, 0.05, 1.0);
    float speedGClamped = clamp(p_speedG, 0.05, 1.0);
    float speedBClamped = clamp(p_speedB, 0.05, 1.0);
    float minSpeed = min(speedRClamped, min(speedGClamped, speedBClamped));
    float waveTime = mix(0.1, 0.9, minSpeed);

    float waveProgress = progress / max(waveTime, 0.001);

    // Vertical offset. `max(waveProgress - uv.y, 0)` is non-zero only
    // where the wave has crossed the current y. The two-step
    // normalisation gives an offset that ramps from 0 to (1-waveTime)
    // as progress goes from 0 to 1 — at progress=1 the offset is
    // ~(1-waveTime), enough to lift content off the top edge by
    // about one window-minus-waveTime height.
    float offset = max(waveProgress - uv.y, 0.0);
    // Floor the denominator so an out-of-range minSpeed that drives
    // waveTime to >=1.0 (e.g. a future UI that lets p_speedR/G/B exceed 1)
    // does not divide by zero. At waveTime=1.0 exactly, `(1/1)-1=0` —
    // without the floor `offset/0` produces Inf and the texture sample
    // walks off the surface with NaN UVs.
    offset /= max((1.0 / max(waveTime, 0.001)) - 1.0, 0.001);
    offset *= (1.0 - waveTime);

    // Per-channel offset multipliers using the per-channel CLAMPED
    // speeds. Slowest channel gets x1, faster channels get higher
    // multipliers so they trail upward. The clamps above guarantee
    // mulR/G/B are non-negative without further defence in depth.
    float mulR = speedRClamped - minSpeed + 1.0;
    float mulG = speedGClamped - minSpeed + 1.0;
    float mulB = speedBClamped - minSpeed + 1.0;

    // boundaryMask (see noise.glsl) crops each per-channel sample
    // outside [0, 1] — the lift-off offset carries each sample UV
    // past the top of the surface at different speeds, and
    // uTexture0's clamp-to-edge sampler would otherwise smear the
    // top row of texels into the lifted region, producing chromatic
    // streaks pinned to the top edge. The downstream `edgeFade`
    // smoothstep masks the OUTPUT uv, not the sample UVs, so it
    // doesn't address this on its own.
    vec2 sampleR = uv + vec2(0.0, offset * mulR);
    vec2 sampleG = uv + vec2(0.0, offset * mulG);
    vec2 sampleB = uv + vec2(0.0, offset * mulB);
    vec4 colorR = surfaceColor(sampleR) * boundaryMask(sampleR);
    vec4 colorG = surfaceColor(sampleG) * boundaryMask(sampleG);
    vec4 colorB = surfaceColor(sampleB) * boundaryMask(sampleB);

    // Recombine. The texture is pre-multiplied; un-premultiply each
    // channel against its OWN alpha (those are the only valid divisors
    // — averaging the alphas first and reapplying it later breaks the
    // pre-multiplied invariant `rgb ≤ alpha` on translucent windows
    // and surfaces a hue-shifted halo around the silhouette). Re-
    // premultiply against the average so the final fragColor stays
    // pre-multiplied for the daemon's blend pipeline.
    float a = (colorR.a + colorG.a + colorB.a) / 3.0;
    vec3 straight = vec3(
        colorR.a > 1e-4 ? colorR.r / colorR.a : 0.0,
        colorG.a > 1e-4 ? colorG.g / colorG.a : 0.0,
        colorB.a > 1e-4 ? colorB.b / colorB.a : 0.0
    );
    vec3 rgb = straight * a;

    // Brightness pulse, peaks mid-leg.
    rgb *= mix(1.0, p_brightness, fadeInOut(progress, 4.0));

    // Surface-edge fade in pixel space (~30 px from each edge) so the
    // rising-off-top reading is clean. Floor `iResolution` so a first-
    // frame zero-sized surface doesn't collapse `edgePx`/`edgeFar` to
    // zero — the smoothstep would then return 0 everywhere and the
    // entire window would render fully transparent for one paint.
    vec2 res = max(iResolution, vec2(1.0));
    vec2 edgePx = uv * res;
    vec2 edgeFar = (vec2(1.0) - uv) * res;
    float edgeFade = smoothstep(0.0, 30.0, edgePx.x) *
                     smoothstep(0.0, 30.0, edgePx.y) *
                     smoothstep(0.0, 30.0, edgeFar.x) *
                     smoothstep(0.0, 30.0, edgeFar.y);

    return vec4(rgb, a) * edgeFade;
}
