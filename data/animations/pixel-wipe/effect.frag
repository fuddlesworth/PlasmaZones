// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixel Wipe — pixelated radial dissolve. A pixelation+dissolution
// wavefront emanates from a configurable origin point, with the
// transition zone showing chunky pixels noise-keyed for randomness.
// Visually inspired by Burn-My-Windows (pixel-wipe.frag, Simon
// Schneegans), written natively against our `iTime`/`uTexture0`.
//
// ## iTime convention
//
// `bmwProgress = easeOutQuad(iTime)` — same direction as BMW open
// (window forms from origin outward). On show, the solid region
// expands from the origin point. On hide, the dissolved region
// contracts toward the origin.
//
// We chose Format-A (forward-mapped iTime → bmwProgress) over
// Format-B (`bmwProgress = easeOutQuad(1-iTime)`, which would
// match BMW close on hide instead). With Format-A the show is
// the more conventional "window materializes from a point and
// expands" reading; the hide reads as "window collapses inward
// to a point", which is also natural. Either formulation has to
// invert one direction since the spatial wavefront can only
// move outward in one direction of a single function-of-iTime.

#include <noise.glsl>

// `p_maxPixelSize` / `p_originX` / `p_originY` are generated from
// metadata.json (the customParams[0] sub-slots) by the harness.

const float FADE_WIDTH = 1.0;

float easeOutQuad(float x) { return -1.0 * x * (x - 2.0); }

// hash22 + simplex2D + simplex2DFractal hosted in shared/noise.glsl.
// 4-octave fractal simplex gives the chunky "burn pattern" for the
// dissolve threshold randomness rather than a smooth gradient.

vec4 pTransition(vec2 uv, float t)
{
    vec2 origin = vec2(p_originX, p_originY);

    // Per-pixel distance from the origin. Window-corner case: max
    // distance is sqrt(2) ≈ 1.414 (diagonal of unit square).
    float circle = length(uv - origin);

    // Same formula as BMW open direction: solid region expands from
    // origin outward as bmwProgress goes 0→1 (which corresponds to
    // iTime going 0→1, our show direction).
    float bmwProgress = easeOutQuad(clamp(iTime, 0.0, 1.0));
    float gradient    = ((1.0 - bmwProgress) * (1.0 + FADE_WIDTH) - 1.0 + circle) / FADE_WIDTH;
    float dissolve    = smoothstep(0.0, 1.0, gradient);

    // Pixelation grows with the per-pixel dissolve amount, so cells
    // currently in the transition wave are chunkily pixelated while
    // already-solid (dissolve->0) and already-gone (dissolve->1)
    // regions stay at their natural pixel size. The max(.., 1.0)
    // defends against a metadata-bypass that pushes maxPixelSize
    // negative; matches sibling pixelate's defence at line 50.
    float pixelSize = max(ceil(p_maxPixelSize * dissolve + 1.0), 1.0);
    // Floor iResolution so an early-frame zero-sized surface doesn't
    // divide-by-zero into an infinite pixelGrid.
    vec2 pixelGrid  = vec2(pixelSize) / max(iResolution, vec2(1.0));
    vec2 cellUV     = uv - mod(uv, pixelGrid) + pixelGrid * 0.5;
    // boundaryMask (see noise.glsl) crops the right/bottom-edge cell
    // whose centre can exceed 1.0 by up to half a cell. Without it,
    // uTexture0's clamp-to-edge sampler returns the last-column /
    // last-row texel for that cell, smearing the window's edge alpha
    // into a ~½-cell-wide band past the surface boundary.
    vec4 sampled    = surfaceColor(cellUV) * boundaryMask(cellUV);

    // Per-cell noise threshold gates the dissolve so the wavefront
    // is a chunky speckled pattern rather than a smooth ring. A cell
    // becomes transparent when dissolve > random_for_this_cell, with
    // a soft falloff over `1/10` of the dissolve range.
    float random = simplex2DFractal(cellUV * iResolution / 20.0) * 1.5 - 0.25;
    if (dissolve > random) {
        sampled *= max(0.0, 1.0 - (dissolve - random) * 10.0);
    }

    return sampled;
}
