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
    // Kwin-effect path: actor expansion is implemented at quad-
    // construction time. PlasmaZonesEffect::apply() (paint_pipeline.cpp)
    // rebuilds the window's quads at `(1+2·ring) × frame` size and
    // remaps the texCoord to `[-ring, 1+ring]`, so `vTexCoord` already
    // arrives in anchor-space coordinates. iAnchorPosInFbo is pushed as
    // (0, 0) and iAnchorSize == iResolution, so the math collapses to
    // anchorUv == vTexCoord — same `[-ring, 1+ring]` range the daemon
    // path produces via uniform-driven remap. Different runtime
    // mechanism, same shader source.
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
    // warp would have displaced it (first-order inverse). A backward
    // warp with magnitude `strength` can only pull `sampleUv` inside
    // the anchor's `[0, 1]` square from up to `strength` distance into
    // the ring — fragments past that range have no warp-displaced
    // content to show, and rendering anything there (smearing the
    // anchor's edge texel via clamp-to-edge, or sampling outside the
    // FBO via wrap) produces visible bleed.
    //
    // Mask the sample by a feathered alpha that runs over exactly the
    // warp's reach. Inside the anchor (with feather slack on the
    // border for sub-pixel AA), `mask == 1` and the warp ripples the
    // content normally. From the anchor edge outward into the ring,
    // `mask` smoothstep's from 1 to 0 over `feather` units — which
    // matches `strength` so the fade exactly covers the area where
    // warp can possibly reach inward. Beyond that, `mask == 0`: the
    // deep ring is transparent regardless of what the texture sample
    // returns there. `clamp(sampleUv)` keeps the texture access
    // in-bounds so wrap-mode garbage doesn't influence the result
    // even with `mask == 0` (defence in depth — masked sample value
    // shouldn't matter, but a NaN sample multiplied by 0 is still NaN).
    //
    // The 0.005 lower bound stays for the inside-the-anchor case where
    // strength == 0 mid-paint (envelope at the endpoints): without it
    // the smoothstep collapses to a step at 0 / 1 and the rendered
    // anchor would lose sub-pixel AA at its outer edge.
    vec2 sampleUv = anchorUv - warp;
    float feather = max(strength, 0.005);
    vec2 lo = smoothstep(vec2(-feather), vec2(0.0), sampleUv);
    vec2 hi = vec2(1.0) - smoothstep(vec2(1.0), vec2(1.0) + vec2(feather), sampleUv);
    float mask = lo.x * lo.y * hi.x * hi.y;
    fragColor = texture(uTexture0, clamp(sampleUv, vec2(0.0), vec2(1.0))) * mask;
}
