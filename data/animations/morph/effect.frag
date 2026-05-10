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

    // Effective ring fraction — same value the runtime baked into the
    // FBO layout, recovered from the uniform ratio so the shader and
    // the runtime can't drift. `anchorTopLeftUv = ring / (1 + 2·ring)`,
    // solve for ring: ring = anchorTopLeftUv / anchorSizeUv = (padW /
    // FBO) / (anchorW / FBO) = padW / anchorW. Independent per-axis.
    // Zero on the kwin path (anchorTopLeftUv == 0) and on the daemon
    // path with zero ring; the depth-fade math below collapses to a
    // straight-through alpha=1 in either case.
    vec2 ring = anchorTopLeftUv / anchorSizeUv;

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

    // Sample with `sampleUv` clamped to the anchor's natural UV range.
    // For fragments inside the anchor's `[0, 1]` square, this lets the
    // warp ripple the content as before (backward warp). For fragments
    // in the ring (`anchorUv` outside `[0, 1]`), the clamp pulls the
    // sample from the anchor's edge so the ring gets a warp-displaced
    // copy of the silhouette boundary — without this clamp, the
    // previous `boundaryMask`-based variant zeroed every fragment
    // beyond `warpStrength` distance into the ring (default
    // warpStrength = 0.1 left 80% of a `fboExtent: "anchor+0.5"` ring
    // blank, the visible "morph hits an edge at ~10% of the way out"
    // symptom).
    vec2 sampleUv = clamp(anchorUv - warp, vec2(0.0), vec2(1.0));

    // Ring-depth fade: alpha 1 inside the anchor, smoothly down to 0
    // at the outer ring edge. Without this, the clamped sample above
    // would smear the anchor's edge texel uniformly across the entire
    // ring (the "edge-smear past warp" symptom that the original
    // boundaryMask was preventing). The smoothstep falls off over the
    // full ring width on each axis so a `ring = 0.5` extent fades
    // gracefully from anchor edge to FBO edge.
    //
    // Depth = how far the fragment sits OUTSIDE `[0, 1]` on each axis,
    // normalised by the ring fraction on that axis. ring.x can be 0
    // (no padding on this axis) — guard the division.
    vec2 ringEpsilon = max(ring, vec2(1.0e-4));
    vec2 outsideAnchor = max(-anchorUv, anchorUv - vec2(1.0));
    vec2 depth = clamp(outsideAnchor / ringEpsilon, vec2(0.0), vec2(1.0));
    float ringAlpha = (1.0 - smoothstep(0.0, 1.0, depth.x)) * (1.0 - smoothstep(0.0, 1.0, depth.y));

    fragColor = texture(uTexture0, sampleUv) * ringAlpha;
}
