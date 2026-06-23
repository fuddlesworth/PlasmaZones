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

#include <noise.glsl>

// `p_warpStrength` / `p_warpFrequency` are generated from metadata.json
// (the customParams[0] sub-slots) by the harness.

#include <anchor_remap.glsl>

vec4 pTransition(vec2 uv, float t)
{
    vec2 anchorUv = anchorRemap(uv);

    // Envelope peaks at iTime == 0.5 (mid-transition) and returns to
    // 0 at the endpoints. Same shape as glitch; gives both show and
    // hide a "warp peak then settle" feel.
    float visibility = clamp(iTime, 0.0, 1.0);
    float envelope = sin(visibility * 3.14159);
    float strength = p_warpStrength * envelope;
    float freq = max(p_warpFrequency, 1.0);

    vec2 warp = vec2(
        sin(anchorUv.y * freq * 6.28318 + iTime * 6.28318) * strength,
        cos(anchorUv.x * freq * 6.28318 + iTime * 6.28318) * strength
    );

    // Geometry-warp: at output anchorUv, sample BACK from where the
    // warp would have displaced it (first-order inverse).
    //
    // Two separate concerns the shader has to handle without either
    // cliffing or smearing edge texels into the ring:
    //
    // 1. Out-of-anchor texture sampling. The redirected texture is
    //    bound with GL_CLAMP_TO_EDGE, so any `texture(uTexture0, uv)`
    //    with uv outside `[0, 1]` returns the anchor's edge texel,
    //    which for opaque-edged content (terminal background, app
    //    chrome, rounded card borders) is a solid colour that smears
    //    visibly into the ring. Kill those samples with a HARD
    //    `step`-based mask on `sampleUv`. The boundary is sharp on
    //    purpose: any partial value here would multiply the edge
    //    texel by a non-zero alpha and leak grey/border colour.
    //
    // 2. Visual fade from the warped anchor into the empty ring. The
    //    backward-warp can only carry content `strength` units into
    //    the ring, so the ring is mostly empty regardless of how the
    //    `inside` mask above filtered the sample. A `boundaryMask`-
    //    style hard cliff on `anchorUv` produced the visible edge the
    //    user reported pre-feather; a smoothstep over `[1, 1+feather]`
    //    feathers the anchor's outer edge into the ring instead. The
    //    feather width tracks `strength` so the fade exactly matches
    //    the warp's actual reach: where the warp can carry content,
    //    the alpha is partial; where it can't, the alpha is 0 anyway.
    //
    // The 0.005 lower bound on `feather` covers the envelope-zero
    // endpoints (start / end of the leg) where `strength == 0` would
    // otherwise collapse the smoothstep to a step at the anchor edge
    // and lose sub-pixel AA on the silhouette.
    vec2 sampleUv = anchorUv - warp;
    vec2 sampleInside = step(vec2(0.0), sampleUv) * step(sampleUv, vec2(1.0));
    vec4 warpedSample = surfaceColor(sampleUv) * sampleInside.x * sampleInside.y;

    float feather = max(strength, 0.005);
    vec2 lo = smoothstep(vec2(-feather), vec2(0.0), anchorUv);
    vec2 hi = vec2(1.0) - smoothstep(vec2(1.0), vec2(1.0) + vec2(feather), anchorUv);
    float mask = lo.x * lo.y * hi.x * hi.y;
    return warpedSample * mask;
}
