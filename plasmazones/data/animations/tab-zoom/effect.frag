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
    // No snapshot of the outgoing tab: bail to the arriving tab at the
    // fragment's own uv. oldColor's built-in fallback substitutes the live
    // content at the DISPLACED coordinate, which here would zoom a second
    // copy of the arriving tab toward the viewer — a duplicate-window
    // artifact, not a graceful degradation.
    if (iHasOldWindow == 0) {
        return surfaceColor(uv);
    }

    float p = clamp(t, 0.0, 1.0);
    float amt = clamp(p_amount, 0.0, 0.6);

    // Scales as seen on screen: the outgoing tab grows past its rect, the
    // incoming one starts inside it and settles at 1. Sampling divides by the
    // scale, so a scale above 1 reads a smaller region and magnifies it.
    float outScale = mix(1.0, 1.0 + amt, p);
    float inScale = mix(1.0 - amt, 1.0, p);
    // Floors that can only ever be defence in depth: with amt capped at 0.6
    // here and p clamped above, the smaller of the two never falls below 0.4.
    // Raise the cap past 1.0 without this and the divide becomes a NaN and
    // the column goes black.
    vec2 outUv = (uv - vec2(0.5)) / max(outScale, 0.05) + vec2(0.5);
    vec2 inUv = (uv - vec2(0.5)) / max(inScale, 0.05) + vec2(0.5);

    // The exponent solves pow(crossPoint, k) == 0.5, so the endpoints stay
    // exactly 0 and 1 while the balance point moves. Clamped IN THE SHADER:
    // the metadata bounds are advisory only — the range check in the shader
    // library has no runtime caller, and a hand-edited profile or a D-Bus
    // param write reaches this uniform unclamped. crossPoint = 1.0 would be
    // log(1) = 0, a divide by zero, and a NaN column; 0.0 would silently pin
    // the blend at "arrived instantly". The clamp makes both unrepresentable.
    float cp = clamp(p_crossPoint, 0.2, 0.8);
    float inW = pow(p, log(0.5) / log(cp));

    // The incoming side's mask cannot silently remove weight: its sample
    // leaves [0,1] in a border ring for the whole leg (inScale < 1), and the
    // first version multiplied the mask into ONLY the incoming term — so in
    // that ring the two weights summed below 1 and the wallpaper showed
    // through the column's border at up to half strength for most of the
    // swap. Folding the mask into the weight and handing the REMAINDER to the
    // outgoing side keeps the pair a partition of unity everywhere: the
    // outgoing sample is always in-bounds (outScale >= 1), so it can always
    // carry what the ring gave up. Endpoints hold: at p = 0, wIn = 0; at
    // p = 1, inUv == uv so the mask is 1 and wIn = 1.
    float wIn = boundaryMask(inUv) * inW;
    return surfaceColor(inUv) * wIn + oldColor(outUv) * (1.0 - wIn);
}
