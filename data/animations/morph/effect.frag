// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Morph transition — sine-based UV displacement field warps the
// rendered surface (sampled through uTexture0). The previous stub
// emitted `vec4(warpedUv.x, warpedUv.y, warpMask, alpha)` — a
// rainbow-like gradient of the UV coords — instead of sampling
// anything, which is the "weird gradient that shows then QML
// renders" report. Now: sample uTexture0 at the warped UV so the
// surface ITSELF deforms during the transition, then settles back
// to its un-warped self as `iTime` reaches the leg endpoints
// (sin envelope peaks at iTime==0.5).

#version 450

#include <animation_uniforms.glsl>
#include <noise.glsl>

// metadata.json declaration order → customParams[0] sub-slots
#define warpStrength  customParams[0].x
#define warpFrequency customParams[0].y

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 fragColor;

void main()
{
    // Remap padded vTexCoord → anchor-space UV. SurfaceAnimator expands
    // the shaderItem QUAD by the metadata's `boundsPadding` value and
    // pushes `iAnchorPosInFbo` = (padW, padH) plus `iAnchorSize` = the
    // captured-anchor pixel size, with Qt auto-deriving `iResolution`
    // from the padded shaderItem bounds. The unified remap below
    // produces the same anchorUv range as the previous `vTexCoord *
    // (1+2pad) - pad` form (`[-pad, 1+pad]` for `boundsPadding=0.5`)
    // without depending on the structural `customParams[7].x` slot.
    //
    // Kwin-effect path: `iAnchorPosInFbo` is explicitly pushed as
    // (0, 0) by paint_pipeline.cpp (the OffscreenEffect FBO covers
    // window frameGeometry 1:1, so anchor IS the FBO origin), and
    // `iAnchorSize == iResolution == window size`. The remap
    // collapses to identity (anchorUv == vTexCoord), same visual
    // behaviour as the pre-refactor `customParams[7].x = 0` path.
    // Actor-expansion parity with BMW (rendering past frameGeometry
    // bounds on kwin) is a deferred follow-up and would change this
    // push to (padW, padH) without touching the shader source.
    vec2 anchorTopLeftUv = iAnchorPosInFbo / iResolution;
    vec2 anchorSizeUv = iAnchorSize / iResolution;
    vec2 anchorUv = (vTexCoord - anchorTopLeftUv) / anchorSizeUv;

    // Envelope peaks at iTime == 0.5 (mid-transition) and returns to
    // 0 at the endpoints — same shape as glitch, gives both show and
    // hide a "warp peak then settle" feel.
    float visibility = clamp(iTime, 0.0, 1.0);
    float envelope = sin(visibility * 3.14159);
    float strength = warpStrength * envelope;
    float freq = max(warpFrequency, 1.0);

    vec2 warp = vec2(
        sin(anchorUv.y * freq * 6.28318 + iTime * 6.28318) * strength,
        cos(anchorUv.x * freq * 6.28318 + iTime * 6.28318) * strength
    );

    // Geometry-warp: at output anchorUv, sample BACK from where the
    // warp would have displaced it (first-order inverse). Points whose
    // pre-image falls outside [0,1] represent areas of the warped
    // silhouette outside the anchor's content — emit transparent.
    // Padding on shaderItem + matching padding on the surface give
    // the rippled silhouette room to extend OUTSIDE the original
    // anchor rectangle without clipping.
    vec2 sampleUv = anchorUv - warp;
    // boundaryMask: see noise.glsl. Crops off-window samples to transparent.
    fragColor = texture(uTexture0, sampleUv) * boundaryMask(sampleUv);
}
