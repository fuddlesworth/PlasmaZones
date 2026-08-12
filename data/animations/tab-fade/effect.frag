// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Tab Fade — the built-in default for `scrolling.tabSwitch`.
//
// A tab swap is the one transition whose two endpoints are both real, live
// window content at the SAME rect: the outgoing tab parks off-canvas and the
// incoming one takes the rectangle it vacated. There is no motion to draw,
// only a change of contents, so a straight cross-fade is the whole effect.
//
// uOldWindow holds the OUTGOING tab, not this window's own earlier content —
// the one place the shared old-content sampler is fed from a different window.
// oldColor()'s fold and Y-flip still apply unchanged, because the capture is
// taken over the arriving window's expanded rect translated to the outgoing
// tab's position, so the two share a frame-relative coordinate system.
//
// Tab-only pack, hence compositor-only (shaderEffectIsCompositorOnly): the
// daemon never bakes it, so the binding-less uOldWindow declaration in
// old_content.glsl needs no PLASMAZONES_KWIN guard.
//
// Geometry and texture coordinates coincide, so the samplers take uv directly.

// The harness supplies #version, <animation_uniforms.glsl>, the in/out and
// main(). old_content.glsl (uOldWindow + oldColor) is pack-specific.
#include <old_content.glsl>

// p_crossPoint (customParams[0].x) is generated from metadata.json.

vec4 pTransition(vec2 uv, float t) {
    // Clamped, unlike the geometry packs': there is no rect to extrapolate
    // here, and an overshooting curve would otherwise drive one weight past 1
    // and the other below 0 — on premultiplied content that reads as the
    // arriving tab flaring over-bright while the departing one goes inverted.
    // A tab swap has no visual meaning outside its endpoints, so this is where
    // it stops.
    float p = clamp(t, 0.0, 1.0);

    // The two weights SUM TO EXACTLY 1 at every p, which is the property that
    // matters: both tabs are opaque, and any pairing that dips below 1 in the
    // middle shows the wallpaper through the column for those frames.
    //
    // p_crossPoint is where the pair is evenly balanced. The exponent solves
    // pow(crossPoint, k) == 0.5, so the endpoints stay exactly 0 and 1 while
    // the balance point moves: below 0.5 the incoming tab asserts itself early
    // (a quick reveal that lingers), above it the outgoing tab holds on.
    // log(crossPoint) is safely non-zero — metadata.json bounds the parameter
    // to [0.2, 0.8].
    float inW = pow(p, log(0.5) / log(p_crossPoint));
    float outW = 1.0 - inW;

    return surfaceColor(uv) * inW + oldColor(uv) * outW;
}
