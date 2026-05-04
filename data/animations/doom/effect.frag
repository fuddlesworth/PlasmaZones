// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Doom — pixel-column melt. Each vertical column of cells slides
// downward at a noise-randomized speed, while pixel cell size
// grows with progress so the window dissolves into chunky
// descending columns. Visually inspired by the equivalent effect
// in Burn-My-Windows (doom.frag, Simon Schneegans), written
// natively against our `iTime`/`iChannel0` contract.
//
// ## iTime convention
//
// `progress = 1 - clamp(iTime, 0, 1)` — 0 at visible, 1 at
// destroyed. Every distortion magnitude scales with `progress`,
// so on show (iTime 0→1) columns rise into place from below; on
// hide (iTime 1→0) columns slide off the bottom. No direction
// branching needed.
//
// At progress=0 the noise term is gated to zero so the window
// settles to a perfect rest state — no per-column jitter at the
// solid endpoint. BMW's original formulation leaves a tiny
// residual jitter at progress=0 (since `shiftProgress = -vScale`
// can leave shift > 0 on noise peaks); we suppress that for
// clean show/hide endpoints.
//
// ## Coordinate space
//
// BMW runs on a doubled-height padded actor (uActorScale=2) so
// columns can overshoot below the window bounds without being
// cropped to the original actor frame. We don't have actor
// padding, so columns that overshoot below `vTexCoord.y = 1` get
// forced transparent via the explicit `inside` clamp. Combined
// with the top/bottom edge mask, the visual reads as columns
// "falling off" the bottom rather than smearing edge texels.

#version 450

#include <animation_uniforms.glsl>

#define maxPixelSize    customParams[0].x
#define horizontalScale customParams[0].y
#define verticalScale   customParams[0].z

layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// 2D simplex noise — MIT-licensed (Inigo Quilez).
vec2 hash22(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * vec3(.1031, .1030, .0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.xx + p3.yz) * p3.zy);
}
float simplex2D(vec2 p) {
    const float K1 = 0.366025404;
    const float K2 = 0.211324865;
    vec2 i  = floor(p + (p.x + p.y) * K1);
    vec2 a  = p - i + (i.x + i.y) * K2;
    float m = step(a.y, a.x);
    vec2 o  = vec2(m, 1.0 - m);
    vec2 b  = a - o + K2;
    vec2 c  = a - 1.0 + 2.0 * K2;
    vec3 h  = max(0.5 - vec3(dot(a, a), dot(b, b), dot(c, c)), 0.0);
    vec3 n  = h * h * h * h *
            vec3(dot(a, -1.0 + 2.0 * hash22(i + 0.0)),
                 dot(b, -1.0 + 2.0 * hash22(i + o)),
                 dot(c, -1.0 + 2.0 * hash22(i + 1.0)));
    return 0.5 + 0.5 * dot(n, vec3(70.0));
}

void main()
{
    vec2 uv = vTexCoord;
    float visibility = clamp(iTime, 0.0, 1.0);
    float progress   = 1.0 - visibility;  // 0 visible, 1 destroyed

    // Pixel cell size grows with progress. ceil ensures whole-pixel
    // cells; at progress=0 this is `ceil(0+1) = 1` so pixelation is a
    // no-op — sampleUV sits at the per-pixel grid centre and the
    // sampler returns the exact source texel.
    float pixelSize = max(1.0, ceil(maxPixelSize * progress + 1.0));
    vec2 pixelGrid  = vec2(pixelSize) / iResolution;

    // Snap to cell centre.
    vec2 cellUV = uv - mod(uv, pixelGrid) + pixelGrid * 0.5;

    // Per-column noise. simplex2D fed `vec2(x*hScale, 0)` collapses
    // to 1D variation along x — same column gets the same noise
    // regardless of y. hScale ties horizontal cell density to the
    // surface width so the number of distinct "fall speeds" stays
    // sensible across window sizes.
    float hScale = horizontalScale * iResolution.x * 0.001;
    float n = simplex2D(vec2(cellUV.x * hScale, 0.0)) * 2.0 - 0.5;

    // Vertical shift. shiftProgress is monotonic in progress, going
    // 0 → 1+vScale. Noise contribution is gated by progress so columns
    // stay still at the visible endpoint (no residual jitter at
    // progress=0).
    float vScale        = verticalScale * iResolution.y * 0.00002;
    float shiftProgress = mix(0.0, 1.0 + vScale, progress);
    float shift         = n * vScale * progress + shiftProgress;

    // Sample from a position higher up in the source — the visible
    // column shows what was originally above its current y, giving
    // the "column fell down" reading. max(shift,0) prevents columns
    // from sliding upward on noise dips.
    vec2 sampleUV = cellUV;
    sampleUV.y -= 0.5 * max(shift, 0.0);

    // Force off-window samples to transparent so columns "fall off"
    // the bottom rather than smearing edge texels via clamp-to-edge.
    vec2 inside    = step(vec2(0.0), sampleUV) * step(sampleUV, vec2(1.0));
    float onScreen = inside.x * inside.y;
    vec4 sampled   = texture(iChannel0, sampleUV) * onScreen;

    // Top/bottom soft edge so the column tops fade rather than pop.
    float edgeFade = smoothstep(0.0, 0.1, uv.y) * smoothstep(0.0, 0.1, 1.0 - uv.y);
    fragColor = sampled * edgeFade;
}
