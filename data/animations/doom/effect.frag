// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Doom — pixel-column melt. Each vertical column of cells slides
// downward at a noise-randomized speed; cell size grows with
// progress; columns separate into chaotic streaks before sliding
// off the bottom. Native port of the BMW close-direction
// behaviour against our `iTime`/`iChannel0` contract.
//
// ## iTime convention
//
// `progress = 1 - clamp(iTime, 0, 1)` — 0 at visible, 1 at
// destroyed. On hide (iTime 1→0) the close formula plays
// forward; on show (iTime 0→1) it plays in reverse so columns
// rise into place from below.
//
// ## Match-BMW choices
//
// * Per-column variation uses 4-octave `simplex2DFractal` (matches
//   BMW). Single-octave gives much smoother variation and the
//   adjacent columns end up too similar to look chaotic.
// * `vScale` keeps BMW's `uActorScale=2` factor folded in
//   (`* 0.00004` rather than `* 0.00002`) — without it, columns
//   only slide half as far as BMW's at the same `verticalScale`.
// * `shiftProgress = mix(-vScale, 1+vScale, progress)` starts
//   negative so noise-driven jitter on individual columns at
//   progress=0 leaves a tiny residual offset. BMW does this and
//   it's part of the look — without the negative start, all
//   columns move together at the same shifted progress and the
//   melt loses its differentiation.
// * Full `max(shift, 0)` subtraction (matches BMW's `0.5 *
//   uActorScale * max(shift, 0)` at uActorScale=2). Halving this
//   collapses the whole melt range.
//
// ## Off-window samples
//
// Columns that overshoot past `vTexCoord.y = 1` get forced
// transparent via the `inside` clamp; without it the
// clamp-to-edge sampler smears the bottom edge texel up the
// column, breaking the "fell off" reading.

#version 450

#include <animation_uniforms.glsl>

#define maxPixelSize    customParams[0].x
#define horizontalScale customParams[0].y
#define verticalScale   customParams[0].z

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

// 4-octave fractal simplex. The fractal layering is what gives
// adjacent columns differentiated fall speeds — single-octave
// noise has too much spatial coherence and the columns end up
// moving together. Same formulation BMW uses.
float simplex2DFractal(vec2 p) {
    mat2 m  = mat2(1.6, 1.2, -1.2, 1.6);
    float f = 0.5000 * simplex2D(p);  p = m * p;
    f      += 0.2500 * simplex2D(p);  p = m * p;
    f      += 0.1250 * simplex2D(p);  p = m * p;
    f      += 0.0625 * simplex2D(p);
    return f;
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

    // Per-column noise via 4-octave fractal. `cellUV.x * hScale`
    // fed as `vec2(x, 0)` collapses to 1D variation along x —
    // same column gets the same noise regardless of y.
    float hScale = horizontalScale * iResolution.x * 0.001;
    float noise  = simplex2DFractal(vec2(cellUV.x * hScale, 0.0)) * 2.0 - 0.5;

    // Vertical shift. The `0.00004` constant folds in BMW's
    // `uActorScale=2` factor (BMW: `* 0.00002 * uActorScale`).
    // shiftProgress starts NEGATIVE at progress=0 — combined with
    // `max(shift, 0)` below, this means most columns sit at zero
    // shift while a few high-noise columns get a small head start.
    // That's the spread that makes adjacent columns separate
    // visibly during the melt.
    float vScale        = verticalScale * iResolution.y * 0.00004;
    float shiftProgress = mix(-vScale, 1.0 + vScale, progress);
    float shift         = noise * vScale + shiftProgress;

    // Sample from a position higher up in the source. BMW does
    // `0.5 * uActorScale * max(shift, 0)` which at uActorScale=2 is
    // a full `1.0 * max(shift, 0)`. Halving this collapses the
    // melt range — at mid-progress the slowest and fastest columns
    // end up only ~20% apart instead of ~50%.
    vec2 sampleUV = cellUV;
    sampleUV.y -= max(shift, 0.0);

    // Force off-window samples to transparent so columns "fall off"
    // the bottom rather than smearing edge texels via clamp-to-edge.
    vec2 inside    = step(vec2(0.0), sampleUV) * step(sampleUV, vec2(1.0));
    float onScreen = inside.x * inside.y;
    vec4 sampled   = texture(iChannel0, sampleUV) * onScreen;

    // Top/bottom soft edge so the column tops fade rather than pop.
    float edgeFade = smoothstep(0.0, 0.1, uv.y) * smoothstep(0.0, 0.1, 1.0 - uv.y);
    fragColor = sampled * edgeFade;
}
