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

#version 450

#include <animation_uniforms.glsl>

#define brightness customParams[0].x
#define speedR     customParams[0].y
#define speedG     customParams[0].z
#define speedB     customParams[0].w

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Bell curve in [0, 1], peaks at t=0.5. `power` controls steepness.
float fadeInOut(float t, float power)
{
    float s = -1.0 * pow((t - 0.5) / 0.5, power) + 1.0;
    return clamp(s, 0.0, 1.0);
}

void main()
{
    vec2 uv = vTexCoord;
    float visibility = clamp(iTime, 0.0, 1.0);
    float progress   = 1.0 - visibility;

    // The wave sweeps faster when minSpeed is small (wave finishes
    // earlier in the timeline). minSpeed clamped above 0 to avoid
    // divide-by-zero in the offset normalisation.
    float minSpeed = max(min(speedR, min(speedG, speedB)), 0.05);
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
    // waveTime to ≥1.0 (e.g. a future UI that lets speedR/G/B exceed 1)
    // does not divide by zero. At waveTime=1.0 exactly, `(1/1)-1=0` —
    // without the floor `offset/0` produces Inf and the texture sample
    // walks off the surface with NaN UVs.
    offset /= max((1.0 / max(waveTime, 0.001)) - 1.0, 0.001);
    offset *= (1.0 - waveTime);

    // Per-channel offset multipliers. Slowest channel gets ×1, faster
    // channels get higher multipliers so they trail upward.
    float mulR = speedR - minSpeed + 1.0;
    float mulG = speedG - minSpeed + 1.0;
    float mulB = speedB - minSpeed + 1.0;

    vec4 colorR = texture(uTexture0, uv + vec2(0.0, offset * mulR));
    vec4 colorG = texture(uTexture0, uv + vec2(0.0, offset * mulG));
    vec4 colorB = texture(uTexture0, uv + vec2(0.0, offset * mulB));

    // Recombine. The texture is pre-multiplied; we extract the
    // per-channel pre-multiplied values and re-emit with the average
    // alpha. This preserves chromatic-aberration look without an
    // un-premultiply / re-premultiply round trip per channel.
    float a = (colorR.a + colorG.a + colorB.a) / 3.0;
    vec3 rgb = vec3(colorR.r, colorG.g, colorB.b);

    // Brightness pulse, peaks mid-leg.
    rgb *= mix(1.0, brightness, fadeInOut(progress, 4.0));

    // Surface-edge fade in pixel space (~30 px from each edge) so the
    // rising-off-top reading is clean.
    vec2 edgePx = uv * iResolution;
    vec2 edgeFar = (vec2(1.0) - uv) * iResolution;
    float edgeFade = smoothstep(0.0, 30.0, edgePx.x) *
                     smoothstep(0.0, 30.0, edgePx.y) *
                     smoothstep(0.0, 30.0, edgeFar.x) *
                     smoothstep(0.0, 30.0, edgeFar.y);

    fragColor = vec4(rgb, a) * edgeFade;
}
