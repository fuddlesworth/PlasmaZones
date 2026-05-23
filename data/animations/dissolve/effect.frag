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

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define grain    customParams[0].x  // noise cell size in normalised UV units
#define softness customParams[0].y  // edge softness

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

// Reference card edge length for pixel-constant `grain` scaling. `grain`
// is interpreted as cell edge as a fraction of an 800-pixel reference
// card (default 0.05 → 40px); the iAnchorSize multiply keeps the
// per-cell pixel size constant on larger / smaller surfaces.
const float kReferenceCardSize = 800.0;

void main()
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.
    vec2 uv = vTexCoord;
    float cellSize = max(grain, 0.01);
    vec2 cell = floor(uv * max(iAnchorSize, vec2(1.0)) / (cellSize * kReferenceCardSize));
    float noise = niriHash(cell);

    // iTime is the per-leg [0,1] progress driven by SurfaceAnimator's
    // shaderTime AnimatedValue. Compare per-cell noise against it with
    // a soft window so each cell flips from "hidden" to "shown" at a
    // different threshold — that's the dissolve effect.
    float visibility = clamp(iTime, 0.0, 1.0);
    float soft = max(softness, 0.001);
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
    fragColor = sampled * (1.0 - gate);
}
