// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Dissolve transition — operates on the rendered surface sampled via
// uTexture0 (SRB binding 7). SurfaceAnimator binds the shaderAnchor's
// live `QSGTextureProvider` through `ShaderEffect::setSourceItem`,
// so the shader sees the current rendered pixels rather than a
// pre-leg snapshot. Per-cell noise gates the surface's alpha against
// `iTime` (per-leg [0,1] progress) so the cells reveal in pseudo-
// random order. The parent surface's own opacity leg drives the
// scene-graph fade, so on hide the dissolve composes with the
// outer fade-out for a coherent direction-aware visual. `grain`
// controls the cell size, `softness` the per-cell edge transition.

#include <noise.glsl>

vec4 pTransition(vec2 uv, float t)
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.
    float cellSize = max(p_grain, 0.01);
    // `p_grain` means cell edge as a fraction of the screen — the
    // iAnchorSize / iSurfaceScreenPos.zw factor converts "fraction of
    // the screen" into "fraction of the surface" so cell pixel size
    // stays constant across popup vs. maximized windows. Floors guard
    // against the pre-first-frame (0,0) state of either uniform.
    vec2 cell = floor(uv * max(iAnchorSize, vec2(1.0))
                         / (cellSize * max(iSurfaceScreenPos.zw, vec2(1.0))));
    float noise = niriHash(cell);

    // iTime is the per-leg [0,1] progress driven by SurfaceAnimator's
    // shaderTime AnimatedValue. Compare per-cell noise against it with
    // a soft window so each cell flips from "hidden" to "shown" at a
    // different threshold — that's the dissolve effect.
    float visibility = clamp(t, 0.0, 1.0);
    float soft = max(p_softness, 0.001);
    float gate = smoothstep(visibility - soft, visibility + soft, noise);

    // Sample the captured surface and gate it on the per-cell noise.
    // `gate` from the smoothstep above runs from 1 (cell still hidden,
    // visibility hasn't reached this cell's noise threshold) down to 0
    // (cell fully revealed). The output uses `(1 - gate)` so the cell
    // alpha runs the visible direction: 0 → hidden, 1 → at full
    // surface alpha. Multiplying both colour and alpha keeps the
    // pre-multiplied-alpha invariant the daemon's blend pipeline
    // expects.
    vec4 sampled = surfaceColor(uv);
    return sampled * (1.0 - gate);
}
