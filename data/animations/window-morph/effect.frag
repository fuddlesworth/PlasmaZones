// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Window-morph fragment shader — shader-driven geometry move/resize.
//
// VERTEX-ONLY: the window's content is never resampled. The window jumps to
// its destination instantly (moveResize), and this shader animates the visual
// transition by interpolating the DRAWN RECT from the OLD frame (iFromRect) to
// the NEW frame (iToRect) by iTime while the live content is drawn at its own
// final scale, anchored to that moving rect and cropped by it. A grow reveals
// more of the content, a shrink crops it, and the pixels themselves are
// identical to the settled window on every frame of the leg.
//
// This replaced a cross-fade that sampled the live content at the INTERPOLATED
// rect's normalised coordinate. That squeezed already-final-size content into
// the old geometry at t = 0 and only reached 1:1 at t = 1, so every snap, tile
// and reflow visibly stretched its content for the whole leg and then snapped
// sharp (discussion #868). Scaling the content was the one thing this pack
// existed to avoid, and normalising into the morphing rect reintroduced it.
//
// No old-content snapshot is sampled, so none is captured for this pack: the
// capture request is gated on the compiled shader linking uOldWindow. Packs
// that DO want the old frame (the cross-fade families) keep it by declaring
// the uniform.
//
// Surface-extent shader: apply() lays an output-spanning quad, so vTexCoord
// spans the host output and iResolution is the output size. iFromRect/iToRect
// are pushed in GLOBAL logical-screen pixels (window frame geometry), so a
// fragment's global screen position is reconstructed from the OUTPUT origin:
//   outputOrigin = iSurfaceScreenPos.xy - iAnchorPosInFbo
// iSurfaceScreenPos.xy is the window/surface origin and iAnchorPosInFbo is the
// window's top-left offset within that output, so their difference is the
// output's global top-left. screenPx = outputOrigin + vTexCoord * iResolution.
//
// Geometry-morph endpoints (logical-screen px, x/y/w/h). Default-block
// uniforms pushed by the kwin-effect paint pipeline.
#ifdef PLASMAZONES_KWIN
// On the UBO branch both rects come from the AnimationUniforms transition
// tail (shared/animation_uniforms.glsl); only the kwin branch declares
// them as default-block uniforms.
uniform vec4 iFromRect;
uniform vec4 iToRect;
#endif

vec4 pTransition(vec2 uv, float t) {
    // `t` is the raw (possibly flipped) iTime the pTransition entry contract
    // hands every symmetric pack — NOT legProgress(). That is correct here:
    // this is a geometry-class pack, and geometry legs always run forward
    // (direction lives in iFromRect/iToRect), so raw t IS forward progress.
    //
    // `t` is deliberately NOT clamped here. iTime leaves [0,1] for an overshooting
    // curve (an underdamped spring, a back / elastic ease), and on THIS pack that
    // overshoot is the whole point: the rect lerp below extrapolates past iToRect,
    // so the window flies a little past its target and springs back — the bounce.
    // Clamping at entry, as this used to, silently ate it.
    //
    // Two things downstream must still see a bounded value, so they take `tc`:
    //   - the rect's SIZE. Extrapolating it linearly is nonsense at a large ratio:
    //     a 400px window maximizing to 3840px computes a width of -116 at t = -0.15.
    //     The max() below stops the NaN but collapses the rect to a 1px axis, so the
    //     mask zeroes and the window VANISHES into a sliver before popping back.
    //     "Before the start" does not mean "negative width".
    // The POSITION carries the bounce, which is where the eye reads it: the window
    // sails past its target and springs back, at its final size.
    float tc = clamp(t, 0.0, 1.0);

    // Fragment's global logical-screen position. Reconstruct from the OUTPUT
    // origin (iSurfaceScreenPos.xy is the window origin; subtracting the
    // window's in-output offset iAnchorPosInFbo yields the output's top-left).
    vec2 outputOrigin = iSurfaceScreenPos.xy - iAnchorPosInFbo;
    vec2 screenPx = outputOrigin + uv * resolutionSafe();

    // Interpolated rect (old -> new), then normalise the fragment into it.
    vec4 rect = vec4(mix(iFromRect.xy, iToRect.xy, t), mix(iFromRect.zw, iToRect.zw, tc));
    vec2 ruv = (screenPx - rect.xy) / max(rect.zw, vec2(1.0));

    // Outside the morphing rect: nothing to draw. Small feather to avoid a
    // hard edge as the rect sweeps.
    //
    // The rect is the window FRAME, and ruv is frame-relative, so the bare
    // [0, 1] range cropped the decoration chain's halo at the frame edge for
    // the whole morph. Widen by the chain's outer margin: surfaceColor() is
    // frame-anchored, so an out-of-range coordinate resolves into the padded
    // canvas's margin band. Zero pad reduces to the plain frame-edge mask.
    vec2 pad = surfacePadRel();
    vec2 fw = max(fwidth(ruv), vec2(1.0e-4));
    vec2 edge = min(smoothstep(vec2(0.0), fw, ruv + pad), smoothstep(vec2(0.0), fw, 1.0 + pad - ruv));
    float mask = edge.x * edge.y;
    if (mask <= 0.0) {
        return vec4(0.0);
    }

    // Content coordinate: normalised against the FINAL size, not the morphing
    // one, and anchored at the morphing rect's origin. That is the whole
    // vertex-only property — the divisor never changes, so the content is
    // sampled at exactly one texel per device pixel for the entire leg, and at
    // t = 1 (rect == iToRect) this reduces to the settled window's own mapping.
    // Sampling at `ruv` instead, as this used to, divides by the morphing size
    // and scales the content by iToRect.zw / rect.zw every frame.
    vec2 cuv = (screenPx - rect.xy) / max(iToRect.zw, vec2(1.0));

    // Crop to the content's own extent as well as the rect. A SHRINK leg has a
    // rect LARGER than the final content for most of its run (unmaximize is the
    // everyday case), and without this the fragments past the content edge
    // sample CLAMP_TO_EDGE and smear the window's last texel column across the
    // remaining band. Cropped, a shrink instead reads as the final-size window
    // travelling to its new home, which is the honest vertex-only answer: there
    // are no pixels for that band, and inventing them by scaling is what this
    // pack is here to avoid.
    // Its OWN fwidth: cuv's derivative is 1 / iToRect.zw while ruv's is
    // 1 / rect.zw, so reusing `fw` would feather this edge by the wrong number
    // of pixels by exactly the ratio the morph is mid-way through.
    vec2 cfw = max(fwidth(cuv), vec2(1.0e-4));
    vec2 cedge = min(smoothstep(vec2(0.0), cfw, cuv + pad), smoothstep(vec2(0.0), cfw, 1.0 + pad - cuv));
    return surfaceColor(cuv) * mask * cedge.x * cedge.y;
}
