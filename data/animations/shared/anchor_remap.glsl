// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// Surface-UV to anchor-relative UV remap. Used by shaders that need to
// convert vTexCoord (which spans the full render-target FBO) into the
// captured anchor's UV space.
//
// `iResolution` is the FBO size; `iAnchorSize` is the anchor (frame)
// size; `iAnchorPosInFbo` is the anchor's top-left within the FBO, in
// logical pixels. When the FBO equals the anchor (daemon anchor-extent)
// the uniforms collapse and `anchorRemap` reduces to identity.
//
// On KWin the redirected FBO covers the EXPANDED rect (frame + shadow)
// even for anchor-extent transitions, so `iAnchorPosInFbo` is the shadow
// inset and the remap is load-bearing here too. Surface-extent on both
// runtimes also relies on this.
//
// Requires: <animation_uniforms.glsl> (provides iResolution,
// iAnchorSize, iAnchorPosInFbo).

#ifndef ANCHOR_REMAP_GLSL
#define ANCHOR_REMAP_GLSL

vec2 anchorRemap(vec2 uv) {
    vec2 resSafe = max(iResolution, vec2(1.0));
    vec2 anchorSizePx = max(iAnchorSize, vec2(1.0));
    vec2 anchorTopLeftUv = iAnchorPosInFbo / resSafe;
    vec2 anchorSizeUv = anchorSizePx / resSafe;
    return (uv - anchorTopLeftUv) / anchorSizeUv;
}

#endif
