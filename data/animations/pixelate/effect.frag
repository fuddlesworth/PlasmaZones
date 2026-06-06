// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixelate transition — operates on the rendered surface (sampled
// through uTexture0 at user-texture binding 7). Block size is driven
// by `iTime` (per-leg progress driven by SurfaceAnimator's shaderTime
// AnimatedValue): SurfaceAnimator runs iTime 0→1 on show and 1→0 on
// hide. With `blockPx = (1 - iTime) * maxBlockSize` the surface
// unpixelates as iTime grows toward 1 (show: pixelated → clear) and
// re-pixelates as iTime shrinks back toward 0 (hide: clear →
// pixelated). The shader is the entire visual transition; the
// parent surface's opacity stays at 1.0 during the leg and snaps to
// the leg's terminal value (0.0 for hide) on completion.
//
// SurfaceAnimator binds the visible card (the `shaderAnchor: true`
// property-tagged item) as a live texture provider via
// `ShaderEffect::setSourceItem` — the anchor's `layer.enabled` is
// flipped to true so `QQuickItem::textureProvider()` returns a
// per-frame FBO that the shader samples through `uTexture0` (SRB
// binding 7). Re-rendered every frame the consumer dirties, so the
// shader always sees the current rendered pixels rather than a
// frozen snapshot. When no explicit `shaderAnchor` is found the leg
// falls back to user-texture-0 (transparent fallback) and the
// shader is a visual no-op.

#include <noise.glsl>

// `p_maxBlockSize` is generated from metadata.json (the customParams[0].x
// sub-slot) by the harness. It is interpreted in NORMALISED UV units
// (0..1 across the surface). 0.1 ≈ a 10×10 block grid at peak pixelation.

vec4 pTransition(vec2 uv, float t)
{

    // iTime is the per-leg progress driven by SurfaceAnimator's
    // shaderTime AnimatedValue: 0→1 on show, 1→0 on hide. Block size
    // = (1 - iTime) * maxBlockSize pixelates strongly at iTime=0 and
    // clears at iTime=1, so the visual reads "pixelated → clear" on
    // show and "clear → pixelated" on hide.
    float visibility = clamp(iTime, 0.0, 1.0);
    float blockPx = (1.0 - visibility) * max(p_maxBlockSize, 0.0);

    // Floor sub-pixel sizes to the per-pixel grid so the shader doesn't
    // collapse to a single sample point at full visibility. Use the
    // smaller of x/y so a tall/narrow surface (e.g. 100×1000) produces
    // square 1-pixel cells on both axes rather than horizontally-
    // elongated ones — `iResolution.x` alone only floors to a y-axis
    // pixel size when the surface is wider than tall.
    blockPx = max(blockPx, 1.0 / max(min(iResolution.x, iResolution.y), 1.0));

    // Snap to cell centre so adjacent pixels in the same cell read the
    // same texel — this is what produces the pixelation visual.
    vec2 cell = (floor(uv / blockPx) + 0.5) * blockPx;

    // boundaryMask (see noise.glsl) crops the right/bottom-edge cell
    // whose centre can exceed 1.0 by up to half a cell. Without it,
    // uTexture0's clamp-to-edge sampler returns the last-column /
    // last-row texel for that cell, smearing the window's edge alpha
    // (rounded corners, drop shadows) into a ~½-cell-wide band past
    // the original surface boundary.
    vec4 sampled = surfaceColor(cell) * boundaryMask(cell);

    // The captured surface already encodes its own alpha; the parent-
    // chain scene-graph opacity is applied at blend time, so the
    // shader emits the sampled colour directly without a manual
    // visibility multiply.
    return sampled;
}
