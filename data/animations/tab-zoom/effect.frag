// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Zoom — the outgoing tab comes toward the viewer and fades out while the
// incoming one rises from behind it into place. A cross-fade with a depth cue,
// for the case where the two tabs are the same application and a plain blend
// leaves it ambiguous which one you are looking at.
//
// Both sides move, in opposite directions, on purpose. Zooming only one leaves
// the other looking pinned to the glass and the pair reads as a fade with an
// artefact; moving both makes the depth the point.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl carries uOldWindow / oldColor; noise.glsl carries
// boundaryMask, which crops the shrunken incoming tab to its own rect.
#include <old_content.glsl>
#include <noise.glsl>

// p_amount / p_crossPoint (customParams[0].xy) come from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    float p = clamp(t, 0.0, 1.0);
    float amt = clamp(p_amount, 0.0, 0.6);

    // Scales as seen on screen: the outgoing tab grows past its rect, the
    // incoming one starts inside it and settles at 1. Sampling divides by the
    // scale, so a scale above 1 reads a smaller region and magnifies it.
    float outScale = mix(1.0, 1.0 + amt, p);
    float inScale = mix(1.0 - amt, 1.0, p);
    // Floors that can only ever be defence in depth: with amt capped at 0.6 by
    // metadata.json and p clamped above, the smaller of the two never falls
    // below 0.4. Raise the cap past 1.0 without this and the divide becomes a
    // NaN and the column goes black.
    vec2 outUv = (uv - vec2(0.5)) / max(outScale, 0.05) + vec2(0.5);
    vec2 inUv = (uv - vec2(0.5)) / max(inScale, 0.05) + vec2(0.5);

    // Weights summing to exactly 1, the same shape tab-fade uses and for the
    // same reason: both tabs are opaque, and a pairing that dips below 1 in the
    // middle shows the wallpaper through the column for those frames. The
    // exponent solves pow(crossPoint, k) == 0.5, so the endpoints stay exactly
    // 0 and 1 while the balance point moves. log(crossPoint) is safely non-zero
    // because metadata.json bounds the parameter to [0.2, 0.8].
    float inW = pow(p, log(0.5) / log(p_crossPoint));
    float outW = 1.0 - inW;

    // Only the SHRUNKEN side needs cropping — it has pulled its edges inside
    // the rect, so without the mask its border texel would smear across the
    // margin it vacated. The magnified side covers the rect entirely.
    return surfaceColor(inUv) * boundaryMask(inUv) * inW + oldColor(outUv) * outW;
}
