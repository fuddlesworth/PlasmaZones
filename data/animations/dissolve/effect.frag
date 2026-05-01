// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Dissolve transition — operates on the rendered surface sampled via
// iChannel0 (SRB binding 7). SurfaceAnimator binds the shaderAnchor's
// live `QSGTextureProvider` through `ShaderEffect::setSourceItem`,
// so the shader sees the current rendered pixels rather than a
// pre-leg snapshot. Per-cell noise gates the surface's alpha against
// `qt_Opacity` so the visual is direction-agnostic: on show the
// cells fade in in pseudo-random order, on hide they fade out the
// same way. `grain` controls the cell size, `softness` the per-cell
// edge transition.

#version 450

#include "../_shared/animation_uniforms.glsl"

// metadata.json declaration order → customParams[0] sub-slots
#define grain    customParams[0].x  // noise cell size in normalised UV units
#define softness customParams[0].y  // edge softness

// Surface texture slot — SurfaceAnimator binds the shaderAnchor's
// `QSGTextureProvider` here (live FBO from layer.enabled=true on
// the anchor). See pixelate/effect.frag for the full binding-7
// rationale.
layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) out vec4 fragColor;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;
    float cellSize = max(grain, 0.01);
    vec2 cell = floor(uv / cellSize);
    float noise = hash(cell);

    // qt_Opacity tracks the leg's animated visibility on both show and
    // hide. Compare per-cell noise against it with a soft window so
    // each cell flips from "hidden" to "shown" at a different
    // qt_Opacity threshold — that's the dissolve effect.
    float visibility = clamp(qt_Opacity, 0.0, 1.0);
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
