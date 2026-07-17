// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Doom — pixel-column melt. Each vertical column of cells slides
// downward at a noise-randomized speed; cell size grows with
// progress; columns separate into chaotic streaks before sliding
// off the bottom. Native port of the BMW close-direction
// behaviour against our `iTime`/`uTexture0` contract.
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

#include <noise.glsl>

// 4-octave fractal simplex (`simplex2DFractal`) hosted in noise.glsl —
// the fractal layering is what gives adjacent columns differentiated
// fall speeds; single-octave noise has too much spatial coherence and
// the columns end up moving together. Same formulation BMW uses.

vec4 pTransition(vec2 uv, float t)
{
    float visibility = clamp(t, 0.0, 1.0);
    float progress   = 1.0 - visibility;  // 0 visible, 1 destroyed

    // Pixel cell size grows with progress. ceil ensures whole-pixel
    // cells; at progress=0 this is `ceil(0+1) = 1` so pixelation is a
    // no-op — sampleUV sits at the per-pixel grid centre and the
    // sampler returns the exact source texel.
    float pixelSize = max(1.0, ceil(p_maxPixelSize * progress + 1.0));
    // Floor iResolution so an early-frame surface that hasn't reported
    // its size (iResolution.x or .y == 0) doesn't divide-by-zero into
    // an infinite pixelGrid OR collapse hScale below into a constant
    // (every column getting the same noise → uniform shift for one
    // frame instead of staggered melt). The first paintable frame
    // replaces this with the real surface dimensions.
    vec2 flooredResolution = resolutionSafe();
    vec2 pixelGrid  = vec2(pixelSize) / flooredResolution;

    // Snap to cell centre.
    vec2 cellUV = uv - mod(uv, pixelGrid) + pixelGrid * 0.5;

    // Per-column noise via 4-octave fractal. `cellUV.x * hScale`
    // fed as `vec2(x, 0)` collapses to 1D variation along x —
    // same column gets the same noise regardless of y.
    float hScale = p_horizontalScale * flooredResolution.x * 0.001;
    float noise  = simplex2DFractal(vec2(cellUV.x * hScale, 0.0)) * 2.0 - 0.5;

    // Vertical shift. The `0.00004` constant folds in BMW's
    // `uActorScale=2` factor (BMW: `* 0.00002 * uActorScale`).
    // shiftProgress starts NEGATIVE at progress=0 — combined with
    // `max(shift, 0)` below, this means most columns sit at zero
    // shift while a few high-noise columns get a small head start.
    // That's the spread that makes adjacent columns separate
    // visibly during the melt.
    float vScale        = p_verticalScale * flooredResolution.y * 0.00004;
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
    vec4 sampled   = surfaceColor(sampleUV) * onScreen;

    // Top/bottom soft edge so the column tops fade rather than pop.
    float edgeFade = smoothstep(0.0, 0.1, uv.y) * smoothstep(0.0, 0.1, 1.0 - uv.y);
    return sampled * edgeFade;
}
