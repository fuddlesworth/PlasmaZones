// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Pixelate transition — operates on the rendered surface (sampled
// through iChannel0 at user-texture binding 7). Block size is driven
// by qt_Opacity rather than iTime so the shader reads as
// "pixelated → clear" on show (qt_Opacity 0→1) and the symmetric
// "clear → pixelated" on hide (qt_Opacity 1→0) without the shader
// having to know the leg direction.
//
// SurfaceAnimator pre-grabs the visible card (the `objectName:
// "shaderAnchor"` item) to a QImage at leg start and uploads it via
// `setUserTexture(0, ...)` so this shader has the surface to sample
// through `iChannel0`. While the grab is in flight (one frame on
// average) the texture is the all-transparent fallback the
// PhosphorRendering::ShaderEffect ships with — the shader still
// produces a sensible output (transparent blocks) instead of the
// rainbow placeholder the previous stub emitted.

#version 450

#include "../_shared/animation_uniforms.glsl"

// metadata.json declaration order → customParams[0] sub-slots.
// `maxBlockSize` is interpreted in NORMALISED UV units (0..1 across
// the surface). 0.1 ≈ a 10×10 block grid at peak pixelation.
#define maxBlockSize customParams[0].x

// User texture slot 0 → SRB binding 7. SurfaceAnimator binds the
// rendered-surface QImage here. The sampler defaults to
// `clampToEdge / linear` per ShaderNodeRhi::ensureUserTextureSampler;
// nearest-neighbour would give crisper blocks but linear hides the
// async-grab fallback frame more gracefully.
layout(binding = 7) uniform sampler2D iChannel0;

layout(location = 0) out vec4 fragColor;

void main()
{
    vec2 uv = gl_FragCoord.xy / iResolution;

    // qt_Opacity threads through Qt's scene-graph opacity multiply for
    // the parent chain. SurfaceAnimator animates `target.opacity` on
    // both show and hide, so qt_Opacity tracks "current visibility":
    //   show: 0 → 1 over the leg
    //   hide: 1 → 0 over the leg
    // Block size = (1 - qt_Opacity) * maxBlockSize gives the same
    // visual contract regardless of direction.
    float visibility = clamp(qt_Opacity, 0.0, 1.0);
    float blockPx = (1.0 - visibility) * max(maxBlockSize, 0.0);

    // Floor sub-pixel sizes to the per-pixel grid so the shader doesn't
    // collapse to a single sample point at full visibility.
    blockPx = max(blockPx, 1.0 / max(iResolution.x, 1.0));

    // Snap to cell centre so adjacent pixels in the same cell read the
    // same texel — this is what produces the pixelation visual.
    vec2 cell = (floor(uv / blockPx) + 0.5) * blockPx;

    vec4 sampled = texture(iChannel0, cell);

    // The captured surface already encodes its own alpha; we don't
    // double-multiply by qt_Opacity here because the parent-chain
    // opacity is also applied at scene-graph blend time.
    fragColor = sampled;
}
