// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Dissolve transition — operates on the rendered surface sampled via
// iChannel0 (SRB binding 7). SurfaceAnimator binds the shaderAnchor's
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

// metadata.json declaration order → customParams[0] sub-slots
#define grain    customParams[0].x  // noise cell size in normalised UV units
#define softness customParams[0].y  // edge softness

// Surface texture slot — SurfaceAnimator binds the shaderAnchor's
// `QSGTextureProvider` here (live FBO from layer.enabled=true on
// the anchor). See pixelate/effect.frag for the full binding-7
// rationale.

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    // UV from the vertex stage; gl_FragCoord/iResolution overshoots [0,1]
    // by DPR on high-DPI displays.
    vec2 uv = vTexCoord;
    float cellSize = max(grain, 0.01);
    vec2 cell = floor(uv / cellSize);
    float noise = hash(cell);

    // iTime is the per-leg [0,1] progress driven by SurfaceAnimator's
    // shaderTime AnimatedValue. Compare per-cell noise against it with
    // a soft window so each cell flips from "hidden" to "shown" at a
    // different threshold — that's the dissolve effect.
    float visibility = clamp(iTime, 0.0, 1.0);
    float soft = max(softness, 0.001);
    float gate = smoothstep(visibility - soft, visibility + soft, noise);

    // Sample the captured surface and gate it on the per-cell noise.
    // gate==0 → cell hidden (alpha 0); gate==1 → cell at full
    // surface alpha. Multiplying both colour and alpha keeps the
    // pre-multiplied-alpha invariant the daemon's blend pipeline
    // expects.
    vec4 sampled = texture(iChannel0, uv);
    fragColor = sampled * (1.0 - gate);
}
